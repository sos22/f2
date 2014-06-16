#include "rpcserver.H"

#include <sys/poll.h>
#include <string.h>

#include "buffer.H"
#include "fields.H"
#include "logging.H"
#include "peername.H"
#include "util.H"
#include "waitbox.H"
#include "waitqueue.H"

#include "list.tmpl"
#include "waitqueue.tmpl"

rpcregistration::rpcregistration(
    rpcserver *_owner)
    : mux(),
      idle(mux),
      owner(_owner),
      content(),
      outstanding(0) {}

void
rpcregistration::start()
{
    auto token(mux.lock());
    outstanding++;
    mux.unlock(&token);
}

void
rpcregistration::finished()
{
    auto token(mux.lock());
    assert(outstanding > 0);
    outstanding--;
    if (outstanding == 0)
        idle.broadcast(token);
    mux.unlock(&token);
}

void
rpcregistration::destroy() {
    /* Stop further invocations */
    owner->deregister(this);
    /* Wait for extant invocations to finish */
    auto token(mux.lock());
    while (outstanding != 0)
        token = idle.wait(&token);
    mux.unlock(&token);
    /* We're done. */
    content.flush();
    delete this;
}

rpcinterface::rpcinterface(
    wireproto::msgtag _tag)
    : tag(_tag) {}

class rpcserver::clientthread : threadfn {
private: thread *thr_;
private: rpcserver *owner;
private: socket_t fd;
private: waitbox<bool> *shutdown;
private: void run();
private: clientthread(
    rpcserver *_owner,
    socket_t _fd,
    waitbox<bool> *_shutdown)
    : owner(_owner), fd(_fd), shutdown(_shutdown) {}
private: bool runcommand(const peername &peer,
                         buffer &incoming,
                         buffer &outgoing,
                         bool *die);
public:  thread *thr() const;
public:  static orerror<clientthread *> spawn(
    rpcserver *owner,
    socket_t fd,
    waitbox<bool> *);
};

bool
rpcserver::clientthread::runcommand(
    const peername &peer,
    buffer &incoming,
    buffer &outgoing,
    bool *die) {
    auto r(wireproto::rx_message::fetch(incoming));
    if (r.isfailure()) {
        *die = r.failure() != error::underflowed;
        return false; }
    assert(r.success() != NULL);
    auto tag(r.success()->t);
    logmsg(loglevel::verbose, "run command " + fields::mk(tag));
    
    auto token(owner->mux.lock());
    auto reg(owner->unknownregistration);
    rpcinterface *iface(&owner->unknowninterface);
    for (auto it(owner->registrations.start());
         !it.finished();
         it.next()) {
        for (auto it2((*it)->content.start());
             !it2.finished();
             it2.next()) {
            if ((*it2)->tag == tag) {
                reg = *it;
                iface = *it2;
                break; } } }
    reg->start();
    owner->mux.unlock(&token);
    
    auto res(iface->message(*r.success(), peer, outgoing));
    
    reg->finished();

    if (res.isjust()) {
        wireproto::err_resp_message(*r.success(), res.just())
            .serialise(outgoing); }
    r.success()->finish();
    return true; }

void
rpcserver::clientthread::run() {
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
                while (runcommand(peer, incoming, outgoing, &die))
                    ;
                if (!outgoing.empty()) {
                    fds[1].events |= POLLOUT; } } } }
    
    logmsg(loglevel::info, fields::mk("client thread finishing"));
    fd.close();
    owner->dying->pushtail(this); }

thread *
rpcserver::clientthread::thr() const {
    return thr_; }

orerror<rpcserver::clientthread *>
rpcserver::clientthread::spawn(
    rpcserver *owner,
    socket_t fd,
    waitbox<bool> *localshutdown) {
    auto res(new clientthread(owner, fd, localshutdown));
    auto t(thread::spawn(res, &res->thr_,
                         "RPC client thread for " + fields::mk(fd.peer())));
    if (t.isjust()) {
        delete res;
        return t.just();
    } else { return res; } }


void
rpcserver::rootthread::run() {
    struct pollfd pfds[3];
    list<clientthread *> threads;
    unsigned nrfds;
    pfds[0] = owner->shutdown->fd().poll(POLLIN);
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
                if (owner->shutdown->fd().polled(pfds[i])) {
                    logmsg(loglevel::info,
                           fields::mk("control interface received local shutdown"));
                    assert(!(pfds[i].revents & POLLERR));
                    memmove(pfds+i, pfds+i+1, sizeof(pfds[0]) * (nrfds-i-1));
                    nrfds--;
                    /* No longer interested in accepting more clients. */
                    for (unsigned j = 0; j < nrfds; j++) {
                        if (owner->sock.polled(pfds[j])) {
                            memmove(pfds + j,
                                    pfds + j + 1,
                                    sizeof(pfds[0]) * (nrfds - j - 1));
                            if (i >= j) i--;
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
                    if (threads.empty() && owner->shutdown->ready())
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
                                                owner->shutdown));
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
    assert(owner->shutdown->ready());
    assert(owner->dying->empty());
    return; }

rpcserver::rootthread::rootthread(
    rpcserver *_owner)
    : owner(_owner) {}

rpcserver::unknowniface::unknowniface()
    : rpcinterface(wireproto::msgtag(0)) {}

maybe<error>
rpcserver::unknowniface::message(
    const wireproto::rx_message &msg,
    const peername &peer,
    buffer &) {
    logmsg(loglevel::failure,
           "Received an unrecognised message type " + fields::mk(msg.t) +
           " from " + fields::mk(peer));
    return error::unrecognisedmessage; }

rpcserver::~rpcserver() {
    assert(!roothandle);
    assert(!shutdown);
    assert(!dying);
    assert(unknownregistration->outstanding == 0);
    unknownregistration->destroy();
    assert(registrations.empty());
}

void
rpcserver::deregister(rpcregistration *what)
{
    auto token(mux.lock());
    for (auto it(registrations.start()); !it.finished(); it.next()) {
        if (*it == what) {
            it.remove();
            mux.unlock(&token);
            return; } }
    mux.unlock(&token);
    abort(); }

rpcserver::rpcserver()
    : mux(),
      registrations(),
      shutdown(NULL),
      dying(NULL),
      sock(),
      roothandle(NULL),
      root(this),
      unknowninterface(),
      unknownregistration(registeriface(unknowninterface)) {}

rpcregistration *
rpcserver::registeriface(
    rpcinterface &ri) {
    return registeriface(multiregistration().add(ri)); }

rpcserver::multiregistration &
rpcserver::multiregistration::add(
    rpcinterface &ri) {
    content.pushtail(&ri);
    return *this; }

rpcserver::multiregistration::~multiregistration() {
    content.flush(); }

rpcregistration *
rpcserver::registeriface(const multiregistration &mr) {
    auto res(new rpcregistration(this));
    auto token(mux.lock());
    for (auto it1(mr.content.start()); !it1.finished(); it1.next()) {
        for (auto it2(registrations.start()); !it2.finished(); it2.next()) {
            for (auto it3((*it2)->content.start());
                 !it3.finished();
                 it3.next()) {
                assert((*it3)->tag != (*it1)->tag); } }
        res->content.pushtail(*it1); }
    registrations.pushtail(res);
    mux.unlock(&token);
    return res;
}

maybe<error>
rpcserver::start(const peername &p, const fields::field &name)
{
    assert(!roothandle);
    assert(!shutdown);
    assert(!dying);
    assert(unknownregistration);

    auto ls(waitbox<bool>::build());
    auto td(waitqueue<clientthread *>::build());
    auto sl(socket_t::listen(p));
    maybe<error> threaderr(Nothing);
    
    if (ls.issuccess() && td.issuccess() && sl.issuccess()) {
        shutdown = ls.success();
        dying = td.success();
        sock = sl.success();
        
        threaderr = thread::spawn(
            &root, &roothandle, name);
        if (threaderr == Nothing) return Nothing; }
    
    if (sl.issuccess()) sl.success().close();
    if (td.issuccess()) td.success()->destroy();
    if (ls.issuccess()) delete ls.success();
    
    shutdown = NULL;
    dying = NULL;
    
    if (ls.isfailure()) return ls.failure();
    else if (td.isfailure()) return td.failure();
    else if (sl.isfailure()) return sl.failure();
    else if (threaderr.isjust()) return threaderr.just();
    else abort(); }

peername
rpcserver::localname() const {
    return sock.localname(); }

void
rpcserver::stop() {
    if (roothandle == NULL) return;
    assert(shutdown != NULL);
    shutdown->set(true);
    roothandle->join();
    roothandle = NULL;
    delete shutdown;
    dying->destroy();
    shutdown = NULL;
    dying = NULL; }

void
rpcserver::destroy() {
    stop();
    delete this; }

template class list<rpcinterface *>;
template class list<rpcregistration *>;
template class list<rpcserver::clientthread *>;
template class waitqueue<rpcserver::clientthread *>;
