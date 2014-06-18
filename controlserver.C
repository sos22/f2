#include "controlserver.H"

#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "buffer.H"
#include "cond.H"
#include "fd.H"
#include "fields.H"
#include "list.H"
#include "listenfd.H"
#include "logging.H"
#include "maybe.H"
#include "mutex.H"
#include "thread.H"
#include "orerror.H"
#include "peername.H"
#include "proto.H"
#include "socket.H"
#include "shutdown.H"
#include "unixsocket.H"
#include "util.H"
#include "waitbox.H"
#include "waitqueue.H"
#include "wireproto.H"

#include "list.tmpl"
#include "waitqueue.tmpl"

#include "fieldfinal.H"

namespace _controlserver {

class controlregistration {
private: controlregistration(const controlregistration &o) = delete;
private: void operator=(const controlregistration &) = delete;
private: controlregistration() = delete;
    
private: class controlserverimpl *owner;
public:  unsigned outstanding;
public:  list<controliface *> interfaces;
public:  cond_t idle;
public:  controlregistration(class controlserverimpl *);
    void destroy();
    ~controlregistration() { assert(outstanding == 0); }
};

class pingiface : public controliface {
public:
    pingiface() : controliface(proto::PING::tag) {}
    maybe<error> controlmessage(const wireproto::rx_message &, buffer &);
};
class unknowniface : public controliface {
public:
    unknowniface() : controliface(wireproto::msgtag(0)) {}
    maybe<error> controlmessage(const wireproto::rx_message &, buffer &);
};
class quitiface : public controliface {
    waitbox<shutdowncode> *s;
    quitiface() = delete;
    quitiface(const quitiface &) = delete;
    void operator=(const quitiface &) = delete;
public:
    quitiface(waitbox<shutdowncode> *_s)
        : controliface(proto::QUIT::tag), s(_s)
        {}
    maybe<error> controlmessage(const wireproto::rx_message &, buffer &);
};

class clientthread : threadfn {
    class controlserverimpl *owner;
    socket_t fd;
    thread *_thr;
    waitbox<bool> *shutdown;
    void run(void);
    bool runcommand(buffer &incoming,
                    buffer &outgoing,
                    bool *die);
    clientthread(
        controlserverimpl *_owner, socket_t _fd, waitbox<bool> *_shutdown)
        : owner(_owner), fd(_fd), _thr(NULL), shutdown(_shutdown) {}
    clientthread(const clientthread &) = delete;
    void operator=(const clientthread &) = delete;
public:
    static orerror<clientthread *> spawn(controlserverimpl *,
                                         socket_t,
                                         waitbox<bool> *);
public: thread *thr() const { return _thr; }
};

class rootthread : public threadfn {
private: controlserverimpl *owner;
private: void run();
public:  rootthread(
    controlserverimpl *_owner)
    : owner(_owner) {}; };

class controlserverimpl {
public:  list<controlregistration *> registrations;
public:  waitbox<bool> *localshutdown;
private: waitbox<shutdowncode> *globalshutdown;
public:  mutex_t mux;
public:  waitqueue<clientthread *> *dying;
public:  listenfd sock;
private: thread *roothandle;
private: rootthread root;
    
public:  unknowniface unknowninterface;
private: pingiface pinginterface;
private: quitiface quitinterface;
public:  controlregistration *unknownregistration;
private: controlregistration *registration;
    
private: void run();
public:  controlserverimpl(waitbox<shutdowncode> *_s)
    : registrations(),
      localshutdown(NULL),
      globalshutdown(_s),
      mux(),
      dying(),
      sock(),
      roothandle(NULL),
      root(this),
      unknowninterface(),
      pinginterface(),
      quitinterface(_s),
      unknownregistration(registeriface(unknowninterface)),
      registration(registeriface(controlserver::multiregistration()
                                 .add(getlogsiface::singleton)
                                 .add(pinginterface)
                                 .add(quitinterface))) {}
private: controlserverimpl(const controlserverimpl &) = delete;
private: void operator=(const controlserverimpl &) = delete;

public:  controlregistration *registeriface(controliface &iface);
public:  controlregistration *registeriface(
    const ::controlserver::multiregistration &iface);

public:  maybe<error> setup(const peername &);
public:  void destroy();
private: ~controlserverimpl() {
            assert(dying == NULL); }
};

controlregistration::controlregistration(controlserverimpl *_owner)
    : owner(_owner),
      outstanding(0),
      interfaces(),
      idle(owner->mux)
{}

void
controlregistration::destroy()
{
    auto token(owner->mux.lock());
    for (auto it(owner->registrations.start()); !it.finished(); it.next()) {
        if (*it == this) {
            it.remove();
            while (outstanding > 0)
                token = idle.wait(&token);
            owner->mux.unlock(&token);
            interfaces.flush();
            delete this;
            return;
        }
    }
    owner->mux.unlock(&token);
    abort();
}

controlregistration *
controlserverimpl::registeriface(controliface &iface) {
    return registeriface(controlserver::multiregistration()
                         .add(iface)); }

controlregistration *
controlserverimpl::registeriface(const controlserver::multiregistration &what) {
    auto reg(new controlregistration(this));
    auto token(mux.lock());
    for (auto it(registrations.start()); !it.finished(); it.next()) {
        for (auto it2((*it)->interfaces.start());
             !it2.finished();
             it2.next()) {
            for (auto it3(what.content.start());
                 !it3.finished();
                 it3.next()) {
                if ((*it2)->tag == (*it3)->tag) {
                    mux.unlock(&token);
                    abort(); } } } }
    
    for (auto it(what.content.start()); !it.finished(); it.next()) {
        reg->interfaces.pushtail(*it); }
    registrations.pushtail(reg);
    mux.unlock(&token);
    return reg; }

void
clientthread::run() {
    peername peer(fd.peer());
    struct pollfd fds[2];
    
    fds[0] = shutdown->fd().poll(POLLIN);
    fds[1] = fd.poll(POLLIN);
    
    buffer outgoing;
    buffer incoming;
    bool die;
    die = false;
    logmsg(loglevel::info, "start client thread " + fields::mk(peer));
    while (!die && !shutdown->ready()) {
        fields::flush();
        auto r(::poll(fds, 2, -1));
        if (r < 0)
            error::from_errno().fatal("poll client");
        if (fds[0].revents) {
            assert(fds[0].revents == POLLIN);
            assert(shutdown->ready());
            fds[0].revents = 0; }
        if (fds[1].revents) {
            if (fds[1].revents & POLLOUT) {
                auto rr(outgoing.send(fd));
                if (rr.isjust()) {
                    rr.just().warn("sending to client " + fields::mk(peer));
                    break; }
                fds[1].revents &= ~POLLOUT;
                if (outgoing.empty()) {
                    fds[1].events &= ~POLLOUT; } }
            if (fds[1].revents & POLLIN) {
                auto t(incoming.receive(fd));
                if (t.isjust()) {
                    t.just().warn("receiving from client " + fields::mk(peer));
                    break; }
                fds[1].revents &= ~POLLIN;
                while (runcommand(incoming, outgoing, &die))
                    ;
                if (!outgoing.empty()) {
                    fds[1].events |= POLLOUT; } } } }
    
    logmsg(loglevel::info, fields::mk("client thread finishing"));
    fd.close();
    auto token(owner->mux.lock());
    owner->dying->pushtail(this);
    owner->mux.unlock(&token); }

bool
clientthread::runcommand(
    buffer &incoming, buffer &outgoing, bool *die) {
    auto r(wireproto::rx_message::fetch(incoming));
    if (r.isfailure()) {
        *die = r.failure() != error::underflowed;
        return false; }
    assert(r.success() != NULL);
    auto tag(r.success()->t);
    logmsg(loglevel::verbose, "run command " + fields::mk(tag));
    
    auto token(owner->mux.lock());
    auto reg(owner->unknownregistration);
    controliface *iface(&owner->unknowninterface);
    for (auto it(owner->registrations.start());
         !it.finished();
         it.next()) {
        for (auto it2((*it)->interfaces.start());
             !it2.finished();
             it2.next()) {
            if ((*it2)->tag == tag) {
                reg = *it;
                iface = *it2;
                break; } } }
    reg->outstanding++;
    owner->mux.unlock(&token);
    
    auto res(iface->controlmessage(*r.success(), outgoing));
    
    auto token2(owner->mux.lock());
    reg->outstanding--;
    if (!reg->outstanding) {
        reg->idle.broadcast(token2); }
    owner->mux.unlock(&token2);
    
    if (res.isjust()) {
        wireproto::err_resp_message(*r.success(), res.just())
            .serialise(outgoing); }
    r.success()->finish();
    return true; }

orerror<clientthread *>
clientthread::spawn(controlserverimpl *owner,
                    socket_t fd,
                    waitbox<bool> *shutdown)
{
    auto work(new clientthread(owner, fd, shutdown));
    auto tr(thread::spawn(work, &work->_thr,
                "control thread for " + fields::mk(fd.peer())));
    if (tr.isjust()) {
        delete work;
        return tr.just();
    } else {
        return work;
    }
}

maybe<error>
pingiface::controlmessage(const wireproto::rx_message &msg, buffer &outgoing)
{
    logmsg(loglevel::info,
           "ping msg " + fields::mk(msg.getparam(proto::PING::req::msg)));
    wireproto::resp_message m(msg);
    static int cntr;
    m.addparam(proto::PING::resp::cntr, cntr++);
    m.addparam(proto::PING::resp::msg, "response message");
    auto r(m.serialise(outgoing));
    if (r.isjust())
        r.just().warn("sending pong");
    return r;
}

maybe<error>
quitiface::controlmessage(const wireproto::rx_message &msg, buffer &)
{
    auto reason(msg.getparam(proto::QUIT::req::reason));
    if (!reason) return error::missingparameter;
    auto message(msg.getparam(proto::QUIT::req::message));
    logmsg(loglevel::notice, "received a quit message: " + fields::mk(message));
    s->set(reason.just());
    return Nothing;
}

maybe<error>
unknowniface::controlmessage(const wireproto::rx_message &msg, buffer &)
{
    logmsg(loglevel::failure,
           "Received an unrecognised message type " + fields::mk(msg.t));
    return error::unrecognisedmessage;
}

void
rootthread::run()
{
    struct pollfd pfds[3];
    list<clientthread *> threads;
    int nrfds;
    pfds[0] = owner->localshutdown->fd().poll(POLLIN);
    pfds[1] = owner->dying->fd().poll(POLLIN);
    pfds[2] = owner->sock.poll();
    nrfds = 3;
    while (1) {
        int r = poll(pfds, nrfds, -1);
        if (r < 0) {
            error::from_errno().fatal("polling control interface"); }
        for (unsigned i = 0; r; i++) {
            assert(i < ARRAY_SIZE(pfds));
            if (pfds[i].revents) {
                if (owner->localshutdown->fd().polled(pfds[i])) {
                    logmsg(loglevel::info,
                           fields::mk("control interface received local shutdown"));
                    assert(!(pfds[i].revents & POLLERR));
                    memmove(pfds+i, pfds+i+1, sizeof(pfds[0]) * (nrfds-i-1));
                    nrfds--;
                    /* No longer interested in accepting more clients. */
                    for (int j = 0; j < nrfds; j++) {
                        if (owner->sock.polled(pfds[j])) {
                            memmove(pfds + j,
                                    pfds + j + 1,
                                    sizeof(pfds[0]) * (nrfds - j - 1));
                            nrfds--;
                            j--;
                        }
                    }
                    if (threads.empty())
/**/                    goto out;
                } else if (owner->dying->fd().polled(pfds[i])) {
                    assert(!(pfds[i].revents & POLLERR));
                    auto thr(owner->dying->pophead());
                    logmsg(loglevel::info,
                           "control interface reaping client thread" +
                           fields::mk(*thr->thr()));
                    thr->thr()->join();
                    for (auto it(threads.start()); 1; it.next()) {
                        assert(!it.finished());
                        if (*it == thr) {
                            it.remove();
                            break;
                        }
                    }
                    delete thr;
                    if (threads.empty() && owner->localshutdown->ready())
/**/                    goto out;
                } else {
                    if (pfds[i].revents & POLLERR)
                        error::disconnected.fatal("on control listen socket");
                    assert(owner->sock.polled(pfds[i]));
                    auto newfd(owner->sock.accept());
                    if (newfd.isfailure())
                        newfd.failure().fatal("accept on control interface");
                    logmsg(loglevel::info,
                           "control interface accepting new client " +
                           fields::mk(newfd.success().peer()));
                    auto tr(clientthread::spawn(owner,
                                                newfd.success(),
                                                owner->localshutdown));
                    if (tr.isfailure()) {
                        error::from_errno().warn(
                            "Cannot build thread for new client " +
                            fields::mk(newfd.success().peer()));
                        newfd.success().close();
                    } else {
                        threads.pushhead(tr.success());
                    }
                }
                pfds[i].revents = 0;
                r--;
            }
        }
    }
out:
    assert(threads.empty());
    assert(owner->localshutdown->ready());
    assert(owner->dying->empty());
    return;
}

/* not a destructor because it has non-trivial synchronisation rules. */
void
controlserverimpl::destroy() {
    if (roothandle != NULL) {
        assert(localshutdown != NULL);
        /* start shutdown */
        localshutdown->set(true);
        /* wait for shutdown to complete */
        roothandle->join();
        sock.close(); }
    
    /* close everything down */
    unknownregistration->destroy();
    registration->destroy();
    if (localshutdown) delete localshutdown;
    if (dying) {
        dying->destroy();
        dying = NULL; }
    delete this;
}

maybe<error>
controlserverimpl::setup(const peername &p) {
    auto ls(waitbox<bool>::build());
    auto td(waitqueue<clientthread *>::build());
    auto sl(socket_t::listen(p));
    maybe<error> threaderr(Nothing);
    
    if (ls.isfailure() || td.isfailure() || sl.isfailure()) {
        goto failed; }
    
    localshutdown = ls.success();
    dying = td.success();
    sock = sl.success();

    threaderr = thread::spawn(
        &root, &roothandle, fields::mk("master control thread"));
    if (threaderr.isjust()) goto failed;
    
    return Nothing;

failed:
    if (sl.issuccess()) sl.success().close();
    if (td.issuccess()) td.success()->destroy();
    if (ls.issuccess()) delete ls.success();
    
    if (sl.isfailure()) return sl.failure();
    else if (td.isfailure()) return td.failure();
    else if (sl.isfailure()) return sl.failure();
    else if (threaderr.isjust()) return threaderr.just();
    else abort(); }

} /* end of _controlserver namespace */

/* The controlserver class itself is just a wrapper around
   controlserverimpl, so that we don't need all the implementation
   details in the controlserver.H header. */
orerror<controlserver *>
controlserver::build(const peername &p, waitbox<shutdowncode> *s)
{
    auto r(new _controlserver::controlserverimpl(s));
    auto e(r->setup(p));
    if (e.isnothing()) {
        return (controlserver *)r;
    } else {
        r->destroy();
        return e.just(); } }
void
controlserver::destroy() {
    ((_controlserver::controlserverimpl *)this)->destroy(); }
controlserver::iface
controlserver::registeriface(
    controliface &interface) {
    return iface::__mk_iface__(
        ((_controlserver::controlserverimpl *)this)->registeriface(interface));}
controlserver::iface
controlserver::registeriface(
    const controlserver::multiregistration &interface) {
    return iface::__mk_iface__(
        ((_controlserver::controlserverimpl *)this)->registeriface(interface));}


controlserver::multiregistration &
controlserver::multiregistration::add(controliface &iface)
{
    content.pushtail(&iface);
    return *this;
}

void
controlserver::iface::deregister() {
    assert(content);
    content->destroy();
    content = NULL; }

template class list<controliface *>;
template class list<_controlserver::controlregistration *>;
template class list<_controlserver::clientthread *>;
template class waitqueue<_controlserver::clientthread *>;
