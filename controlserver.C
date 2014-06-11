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
#include "shutdown.H"
#include "unixsocket.H"
#include "waitbox.H"
#include "waitqueue.H"
#include "wireproto.H"

#include "list.tmpl"
#include "waitqueue.tmpl"

#include "fieldfinal.H"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

class controlthread;

struct control_registration {
    control_registration() __attribute__((noreturn)); /* should never be
                                               * called, but needed
                                               * for linked list
                                               * templates */
    control_registration(const control_registration &o);
    void operator=(const control_registration &) = delete;

    controlthread *server;
    unsigned outstanding;
    list<controliface *> interfaces;
    cond_t idle;
    control_registration(controlthread *_server);
    void deregister() const;
    ~control_registration() { assert(outstanding == 0); }
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

struct controlthread : threadfn {
    class clientthread : threadfn {
        controlthread *owner;
        fd_t fd;
        thread *_thr;
        waitbox<bool> *shutdown;
        const peername peer;
        void run(void);
        bool runcommand(buffer &incoming,
                        buffer &outgoing,
                        bool *die);
        clientthread(controlthread *_owner,
                     fd_t _fd,
                     waitbox<bool> *_shutdown,
                     const peername &_peer)
            : owner(_owner), fd(_fd), _thr(NULL),
              shutdown(_shutdown), peer(_peer)
            {}
        clientthread(const clientthread &) = delete;
        void operator=(const clientthread &) = delete;
    public:
        static orerror<clientthread *> spawn(controlthread *,
                                             fd_t,
                                             waitbox<bool> *,
                                             const peername &peer);
        thread *thr() { return _thr; }
        virtual ~clientthread() {}
    };

    /* A lot of state which would be more natural in controlserver
       actually goes in here because that keeps the controlserver
       interface a bit easier to understand. */
    list<control_registration *> registrations;
    waitbox<bool> *localshutdown;
    waitbox<shutdowncode> *globalshutdown;
    mutex_t mux;
    waitqueue<clientthread *> *dying;
    thread *t;
    listenfd sock;

    unknowniface unknowninterface;
    pingiface pinginterface;
    quitiface quitinterface;
    control_registration *unknownregistration;
    control_registration *loggingregistration;
    control_registration *pingregistration;
    control_registration *quitregistration;

    void run();
    controlthread(waitbox<shutdowncode> *_s)
        : registrations(),
          localshutdown(NULL),
          globalshutdown(_s),
          mux(),
          dying(),
          t(NULL),
          sock(),
          unknowninterface(),
          pinginterface(),
          quitinterface(_s),
          unknownregistration(registeriface(unknowninterface)),
          loggingregistration(registeriface(getlogsiface::singleton)),
          pingregistration(registeriface(pinginterface)),
          quitregistration(registeriface(quitinterface))
        {
        }
    controlthread(const controlthread &) = delete;
    void operator=(const controlthread &) = delete;

    control_registration *registeriface(controliface &iface);
    control_registration *registeriface(
        const ::controlserver::multiregistration &iface);

    maybe<error> setup(const char *name);
    void end();
    ~controlthread()
        {
            assert(!t);
            unknownregistration->deregister();
            loggingregistration->deregister();
            pingregistration->deregister();
            quitregistration->deregister();
            localshutdown->destroy();
            dying->destroy();
        }
};

control_registration::control_registration(controlthread *_server)
    : server(_server),
      outstanding(0),
      interfaces(),
      idle(server->mux)
{}

control_registration::control_registration(const control_registration &o)
    : server(o.server),
      outstanding(0),
      interfaces(),
      idle(server->mux)
{
    assert(!o.outstanding);
    for (auto it(o.interfaces.start()); !it.finished(); it.next())
        interfaces.pushtail(*it);
}

control_registration::control_registration()
    : server(NULL),
      outstanding(0),
      interfaces(),
      idle(server->mux)
{
    abort();
}

void
control_registration::deregister() const
{
    auto token(server->mux.lock());
    for (auto it(server->registrations.start()); !it.finished(); it.next()) {
        if (*it == this) {
            it.remove();
            while (outstanding > 0)
                token = idle.wait(&token);
            server->mux.unlock(&token);
            delete this;
            return;
        }
    }
    server->mux.unlock(&token);
    abort();
}

control_registration *
controlthread::registeriface(controliface &iface)
{
    return registeriface(::controlserver::multiregistration()
                         .add(iface));
}

control_registration *
controlthread::registeriface(const controlserver::multiregistration &what)
{
    auto reg(new control_registration(this));
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
                    abort();
                }
            }
        }
    }
    for (auto it(what.content.start()); !it.finished(); it.next())
        reg->interfaces.pushtail(*it);
    registrations.pushtail(reg);
    mux.unlock(&token);
    return reg;
}

void
controlthread::clientthread::run()
{
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
            fds[0].revents = 0;
        }
        if (fds[1].revents) {
            if (fds[1].revents & POLLOUT) {
                auto rr(outgoing.send(fd));
                if (rr.isjust()) {
                    rr.just().warn("sending to client " + fields::mk(peer));
                    break;
                }
                fds[1].revents &= ~POLLOUT;
                if (outgoing.empty())
                    fds[1].events &= ~POLLOUT;
            }
            if (fds[1].revents & POLLIN) {
                auto t(incoming.receive(fd));
                if (t.isjust()) {
                    t.just().warn("receiving from client " + fields::mk(peer));
                    break;
                }
                fds[1].revents &= ~POLLIN;
                while (runcommand(incoming, outgoing, &die))
                    ;
                if (!outgoing.empty())
                    fds[1].events |= POLLOUT;
            }
        }
    }
    logmsg(loglevel::info, fields::mk("client thread finishing"));
    fd.close();
    auto token(owner->mux.lock());
    owner->dying->pushtail(this);
    owner->mux.unlock(&token);
}

bool
controlthread::clientthread::runcommand(buffer &incoming,
                                        buffer &outgoing,
                                        bool *die)
{
    auto r(wireproto::rx_message::fetch(incoming));
    if (r.isfailure()) {
        *die = r.failure() != error::underflowed;
        return false;
    }
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
                break;
            }
        }
    }
    reg->outstanding++;
    owner->mux.unlock(&token);

    auto res(iface->controlmessage(*r.success(), outgoing));

    auto token2(owner->mux.lock());
    reg->outstanding--;
    if (!reg->outstanding)
        reg->idle.broadcast(token2);
    owner->mux.unlock(&token2);

    if (res.isjust())
        wireproto::err_resp_message(*r.success(), res.just())
            .serialise(outgoing);
    r.success()->finish();
    return true;
}

orerror<controlthread::clientthread *>
controlthread::clientthread::spawn(controlthread *server,
                                   fd_t fd,
                                   waitbox<bool> *shutdown,
                                   const peername &peer)
{
    auto work(new clientthread(server, fd, shutdown, peer));
    auto tr(thread::spawn(work, &work->_thr,
                "control thread for " + fields::mk(peer)));
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
controlthread::run()
{
    struct pollfd pfds[3];
    list<clientthread *> threads;
    int nrfds;
    pfds[0] = localshutdown->fd().poll(POLLIN);
    pfds[1] = dying->fd().poll(POLLIN);
    pfds[2] = sock.poll();
    nrfds = 3;
    while (1) {
        int r = poll(pfds, nrfds, -1);
        if (r < 0)
            error::from_errno().fatal("polling control interface");
        for (unsigned i = 0; r; i++) {
            assert(i < ARRAY_SIZE(pfds));
            if (pfds[i].revents) {
                if (localshutdown->fd().polled(pfds[i])) {
                    logmsg(loglevel::info,
                           fields::mk("control interface received local shutdown"));
                    assert(!(pfds[i].revents & POLLERR));
                    memmove(pfds+i, pfds+i+1, sizeof(pfds[0]) * (nrfds-i-1));
                    nrfds--;
                    /* No longer interested in accepting more clients. */
                    for (int j = 0; j < nrfds; j++) {
                        if (sock.polled(pfds[j])) {
                            memmove(pfds + j,
                                    pfds + j + 1,
                                    sizeof(pfds[0]) * (nrfds - j - 1));
                            nrfds--;
                            j--;
                        }
                    }
                    if (threads.empty())
/**/                    goto out;
                } else if (dying->fd().polled(pfds[i])) {
                    assert(!(pfds[i].revents & POLLERR));
                    auto thr(dying->pophead());
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
                    if (threads.empty() && localshutdown->ready())
/**/                    goto out;
                } else {
                    if (pfds[i].revents & POLLERR)
                        error::disconnected.fatal("on control listen socket");
                    assert(sock.polled(pfds[i]));
                    auto newfd(sock.accept());
                    if (newfd.isfailure())
                        newfd.failure().fatal("accept on control interface");
                    logmsg(loglevel::info,
                           "control interface accepting new client " +
                           fields::mk(newfd.success().peer));
                    auto tr(clientthread::spawn(this,
                                                newfd.success().fd,
                                                localshutdown,
                                                newfd.success().peer));
                    if (tr.isfailure()) {
                        error::from_errno().warn(
                            "Cannot build thread for new client " +
                            fields::mk(newfd.success().peer));
                        newfd.success().fd.close();
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
    assert(localshutdown->ready());
    assert(dying->empty());
    return;
}

void
controlthread::end()
{
    if (t) {
        assert(localshutdown);
        localshutdown->set(true);
        t->join();
        t = NULL;
    }
    sock.close();
}

maybe<error>
controlthread::setup(const char *name)
{
    error err;

    {
        auto ls(waitbox<bool>::build());
        if (ls.isfailure()) {
            err = ls.failure();
            goto failed;
        }
        localshutdown = ls.success();
    }

    {
        auto td(waitqueue<clientthread *>::build());
        if (td.isfailure()) {
            err = td.failure();
            goto failed;
        }
        dying = td.success();
    }

    {
        auto sl(unixsocket::listen(name));
        if (sl.isfailure()) {
            err = sl.failure();
            goto failed;
        }
        sock = sl.success();
    }

    {
        auto ts(thread::spawn(this, &t, fields::mk("master control thread")));
        if (ts.isjust()) {
            err = ts.just();
            goto failed;
        }
    }
    return Nothing;

failed:
    end();
    return err;
}

orerror<controlserver>
controlserver::build(const char *ident, waitbox<shutdowncode> *s)
{
    auto r(new struct controlthread(s));
    auto e(r->setup(ident));
    if (e.isnothing())
        return controlserver(*r);
    else
        return e.just();
}

void
controlserver::destroy() const
{
    controlthread.end();
    delete &controlthread;
}

controlserver::iface
controlserver::registeriface(controliface &interface) const
{
    return iface::__mk_iface__(controlthread.registeriface(interface));
}

controlserver::multiregistration &
controlserver::multiregistration::add(controliface &iface)
{
    content.pushtail(&iface);
    return *this;
}

controlserver::iface
controlserver::registeriface(const controlserver::multiregistration &r) const
{
    return iface::__mk_iface__(controlthread.registeriface(r));
}

template class list<controlthread::clientthread *>;
template class waitqueue<controlthread::clientthread *>;
template class list<control_registration *>;
template class list<controliface *>;
