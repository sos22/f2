#include "rpcclient2.H"

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "buffer.H"
#include "error.H"
#include "logging.H"
#include "nnp.H"
#include "peername.H"
#include "proto2.H"
#include "serialise.H"
#include "thread.H"
#include "util.H"
#include "version.H"

#include "thread.tmpl"

class rpcclient2::workerthread : public thread {
    /* Interfaces exposed to the rest of the system. */
public: rpcclient2 api;
public: asyncconnect connector;

    /* Stuff to do with connecting. */
    /* Small leaf lock protecting connect* */
public: mutex_t connectlock;
    /* Result of the connect, or Nothing if it's not finished yet. */
public: maybe<orerror<void> > connectres;
    /* Publisher notified when connectres becomes non-Nothing. */
public: publisher connectpub;
    /* Who are we connecting to? */
public: const peername peer;

    /* Stuff to do with receiving responses. */
public: mutex_t rxlock;
public: list<nnp<_asynccall> > rxqueue;
public: bool rxstopped;
public: proto::sequencenr nextseqnr;

    /* Stuff to do with sending requests. */
public: mutex_t txlock;
public: buffer txbuffer;
public: publisher txgrew;
public: int fd;

    /* Shutdown machine. */
public: waitbox<void> shutdown;

public: workerthread(const constoken &tok, const peername &p);

public: void run(clientio);

public: orerror<void> queuerx(nnp<rpcclient2::_asynccall> call);
public: void queuetx(const std::function<void (serialise1 &,
                                               mutex_t::token)> &serialise,
                     proto::sequencenr seqnr);

    /* Simple wrapper aroudn getsockopt(SO_ERROR) */
public: static orerror<void> socketerr(int fd, const peername &peer);
public: orerror<int> connect(clientio io);
};

/* Dummy type, just to give us somewhere to expose a nicer API. */
rpcclient2::rpcclient2() {}

nnp<rpcclient2::workerthread>
rpcclient2::worker() const { return *containerof(this, workerthread, api); }

orerror<nnp<rpcclient2> >
rpcclient2::connect(
    clientio io,
    const peername &pn,
    maybe<timestamp> deadline) {
    auto ac(connect(pn));
    auto t(ac->finished());
    if (t == Nothing) {
        subscriber sub;
        subscription ss(sub, ac->pub());
        t = ac->finished();
        while (t == Nothing) {
            if (sub.wait(io, deadline) == NULL) goto timeout;
            t = ac->finished(); } }
    return ac->pop(t.just());
  timeout:
    ac->abort();
    return error::timeout; }

nnp<rpcclient2::workerthread>
rpcclient2::asyncconnect::owner() const {
    return *containerof(this, workerthread, connector); }

rpcclient2::asyncconnect::token::token() {}

maybe<rpcclient2::asyncconnect::token>
rpcclient2::asyncconnect::finished() const {
    return owner()->connectlock.locked<maybe<token> >(
        [this] () -> maybe<token> {
            if (owner()->connectres == Nothing) return Nothing;
            else return token(); }); }

const publisher &
rpcclient2::asyncconnect::pub() const { return owner()->connectpub; }

orerror<nnp<rpcclient2> >
rpcclient2::asyncconnect::pop(token) {
    assert(owner()->connectres.isjust());
    if (owner()->connectres.just().isfailure()) {
        auto res(owner()->connectres.just().failure());
        delete owner();
        return res; }
    else return _nnp(owner()->api); }

void
rpcclient2::asyncconnect::abort() { delete owner(); }

rpcclient2::asyncconnect::~asyncconnect() {}

nnp<rpcclient2::asyncconnect>
rpcclient2::connect(const peername &pn) {
    return thread::start<workerthread>("C:" + fields::mk(pn), pn)->connector; }

template <typename t> orerror<t>
rpcclient2::call(
    clientio io,
    const std::function<void (serialise1 &,
                              mutex_t::token txlock)> &serialise,
    const std::function<orerror<t> (deserialise1 &,
                                    onconnectionthread)> &deserialise,
    maybe<timestamp> deadline) {
    auto c(call(serialise,
                [&deserialise]
                (asynccall<t> &,
                 orerror<nnp<deserialise1> > ds,
                 onconnectionthread oct) {
                    if (ds.isfailure()) return ds.failure();
                    else return deserialise(ds.success(), oct); }));
    auto tok(c->finished());
    if (tok == Nothing) {
        subscriber sub;
        subscription ss(sub, c->pub());
        tok = c->finished();
        while (tok == Nothing) {
            if (sub.wait(io, deadline) == NULL) goto timeout;
            tok = c->finished(); } }
    return c->pop(tok.just());
  timeout:
    c->abort();
    return error::timeout; }

maybe<rpcclient2::_asynccall::token>
rpcclient2::_asynccall::finished() const {
    assert(!aborted);
    return mux.locked<maybe<token> >(
        [this] () -> maybe<token> {
            if (_finished) return token();
            else return Nothing; } ); }

rpcclient2::_asynccall::token
rpcclient2::_asynccall::wait(clientio io) const {
    auto t(finished());
    if (t == Nothing) {
        subscriber sub;
        subscription ss(sub, _pub);
        while (t == Nothing) {
            sub.wait(io);
            t = finished(); } }
    return t.just(); }

template <typename t> void
rpcclient2::asynccall<t>::complete(deserialise1 &ds, onconnectionthread oct) {
    auto dead(mux.locked<bool>([this, &ds, oct] {
                if (!aborted) {
                    res = deserialise(*this, _nnp(ds), oct);
                    _finished = true;
                    _pub.publish();
                    return false; }
                else return true; }));
    if (dead) delete this; }

template <typename t> void
rpcclient2::asynccall<t>::fail(error e, onconnectionthread oct) {
    auto dead(mux.locked<bool>([this, e, oct] {
                if (!aborted) {
                    res = deserialise(*this, e, oct);
                    _finished = true;
                    _pub.publish();
                    return false; }
                else return true; }));
    if (dead) delete this; }

template <typename t> orerror<t>
rpcclient2::asynccall<t>::pop(token) {
    auto _res(res.just());
    delete this;
    return _res; }

/* This gets a clientio token because it can wait for the deserialise
 * method.  That's a bit of an abuse (the deserialise method doesn't
 * get one), but it should be safe, and it's easier than having yet
 * another tag type to represent the actual relationship. */
template <typename t> maybe<orerror<t> >
rpcclient2::asynccall<t>::abort(clientio) {
    assert(!aborted);
    auto dead(mux.locked<bool>(
                  [this] {
                      aborted = true;
                      return res != Nothing; }));
    if (dead) {
        assert(_finished);
        auto _res(res);
        delete this;
        return _res; }
    else return Nothing; }

/* Allocate a sequence number for a call and put it on the pending-RX
 * list.  The call can't be on the RX list already. */
orerror<void>
rpcclient2::workerthread::queuerx(nnp<_asynccall> call) {
    return rxlock.locked<orerror<void> >(
        [this, call] () -> orerror<void> {
            if (rxstopped) return error::shutdown;
            call->seqnr = nextseqnr;
            nextseqnr++;
            rxqueue.pushtail(call);
            return Success; } ); }

void
rpcclient2::workerthread::queuetx(
    const std::function<void (serialise1 &, mutex_t::token)> &serialise,
    proto::sequencenr seqnr) {
    auto notify(
        txlock.locked<bool>(
            [this, seqnr, &serialise]
            (mutex_t::token txtoken) {
                auto wasempty(txbuffer.empty());
                auto startoff(txbuffer.offset());
                serialise1 s(txbuffer);
                /* size gets filled in later */
                proto::reqheader(-1, version::current, seqnr).serialise(s);
                serialise(s, txtoken);
                /* Set size in header. */
                auto sz = txbuffer.offset() - startoff;
                assert(sz < proto::maxmsgsize);
                *txbuffer.linearise<unsigned>(startoff) = (unsigned)sz;
                /* Try a fast synchronous send rather than waking the
                 * worker thread.  No point if there was stuff in the
                 * buffer before we started: whoever put it there
                 * would have tried a sync send when they did it and
                 * that must have failed, so our sync send will almost
                 * certainly also fail. */
                if (wasempty && fd >= 0) (void)txbuffer.sendfast(fd_t(fd));
                /* Only notify if there's stuff left in the buffer for
                 * the worker thread to do.  This'll also cover the
                 * case where sendfast fails. */
                return wasempty && !txbuffer.empty(); } ) );
    if (notify) txgrew.publish(); }

template <typename t> nnp<rpcclient2::asynccall<t> >
rpcclient2::call(
    const std::function<void (serialise1 &, mutex_t::token)> &serialise,
    const std::function<orerror<t> (asynccall<t> &,
                                    orerror<nnp<deserialise1> >,
                                    onconnectionthread)> &deserialise) {
    nnp<asynccall<t> > res(*(new asynccall<t>(deserialise)));
    auto toolate(worker()->queuerx(*res));
    /* We're not on the connection thread here, but if queuerx()
     * failed then the connection thread is shutting down and will
     * never know about this message, so we can safely pretend to
     * be. */
    if (toolate.isfailure()) res->fail(toolate.failure(),
                                       onconnectionthread());
    else worker()->queuetx(serialise, res->seqnr);
    return res; }

orerror<void>
rpcclient2::workerthread::socketerr(int sock, const peername &_peer) {
    int err;
    socklen_t sz(sizeof(err));
    orerror<void> res(Success);
    if (::getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &sz) < 0) {
            /* Kernel implementation detail: not actually possible to
             * get an error here. */
#ifndef COVERAGESKIP
            res = error::from_errno();
            res.failure()
                .warn("getting connect error for " + fields::mk(_peer));
#endif
            }
    else if (err != 0) res = error::from_errno(err);
    return res; }

orerror<int>
rpcclient2::workerthread::connect(clientio io) {
    auto sock(::socket(peer.sockaddr()->sa_family,
                       SOCK_STREAM | SOCK_NONBLOCK,
                       0));
    if (sock < 0) return error::from_errno();
    if (::connect(sock, peer.sockaddr(), peer.sockaddrsize()) < 0 &&
        errno != EINPROGRESS) {
        ::close(sock);
        return error::from_errno(); }
    subscriber sub;
    subscription sssub(sub, shutdown.pub);
    iosubscription completesub(sub, fd_t(sock).poll(POLLOUT));
    orerror<int> res(sock);
    while (!shutdown.ready()) {
        auto ss(sub.wait(io));
        if (ss == &sssub) continue;
        assert(ss == &completesub);
        /* Check for spurious events on the iosub.  Never actually
         * happens, because of implementation details, but it's
         * allowed by the API, so handle it. */
        auto pfd(fd_t(sock).poll(POLLOUT));
        if (::poll(&pfd, 1, 0) < 0) {
#ifndef COVERAGESKIP
            res = error::from_errno();
            break;
#endif
        }
        if (pfd.revents & POLLOUT) break; }
    if (shutdown.ready()) res = error::shutdown;
    else if (res.issuccess()) {
        auto r2(socketerr(sock, peer));
        if (r2.isfailure()) res = r2.failure(); }
    if (res.isfailure()) ::close(sock);
    return res; }

void
rpcclient2::workerthread::run(clientio io) {
    {   auto r(connect(io));
        if (r.isfailure()) {
            connectlock.locked([r, this] {
                    assert(connectres == Nothing);
                    connectres = r.failure(); });
            connectpub.publish();
            return; }
        assert(r.success() >= 0);
        txlock.locked([this, &r] { fd = r.success(); } ); }
    orerror<void> _failure(Success);
    subscriber sub;

    /* Watch for shutdown requests */
    subscription shutdownsub(sub, shutdown.pub);
    if (shutdown.ready()) _failure = error::shutdown;

    /* Keep an input subscription outstanding at all times, even when
     * we're not expecting any replies, so that we notice quickly when
     * connections drop. */
    iosubscription insub(sub, fd_t(fd).poll(POLLIN));

    /* Always have an outsub, but only arm it when there's stuff in
     * the TX queue. */
    iosubscription outsub(sub, fd_t(fd).poll(POLLOUT));
    bool outsubarmed = true;

    /* Watch for more stuff arriving in the TX queue. */
    subscription txgrewsub(sub, txgrew);
    if (!txbuffer.empty()) txgrewsub.set();

    /* Start by sending a HELLO call. */
    maybe<nnp<asynccall<void> > > hellocall(
        api.call<void>(
            [] (serialise1 &s, mutex_t::token) {
                proto::hello::req().serialise(s); },
            []
            (asynccall<void> &,
             orerror<nnp<deserialise1> > d,
             onconnectionthread) -> orerror<void> {
                if (d.isfailure()) return d.failure();
                auto &ds(*d.success());
                proto::hello::resp r(ds);
                if (ds.isfailure()) return ds.failure();
                if (r.min > version::current || r.max < version::current) {
                    return error::badversion; }
                return Success; }));
    maybe<subscription> hellosub(Nothing);
    hellosub.mkjust(sub, hellocall.just()->pub());
    /* Only this thread can finish the call, so usual subscribe race
     * won't happen. */
    assert(!hellocall.just()->finished());

    buffer rxbuffer;

    while (_failure == Success) {
        auto ss(sub.wait(io));
        if (ss == &txgrewsub) {
            if (txbuffer.empty() || outsubarmed) continue;
            /* No point in trying a sendfast() here because whoever
             * added the stuff to the TX queue will have already done
             * one.  Just wait for the iosubscription to fire. */
            outsub.rearm();
            outsubarmed = true; }
        else if (ss == &outsub) {
            assert(!txbuffer.empty());
            assert(outsubarmed);
            outsubarmed = false;
            auto res(txlock.locked<orerror<void> >([this] {
                        return txbuffer.sendfast(fd_t(fd)); }) );
            if (res.isfailure() && res != error::wouldblock) {
                _failure = res.failure(); }
            else if (!txbuffer.empty()) outsub.rearm(); }
        else if (ss == &insub) {
            auto res(rxbuffer.receivefast(fd_t(fd)));
            insub.rearm();
            if (res.isfailure()) {
                if (res != error::wouldblock) _failure = res.failure();
                continue; }
            while (true) {
                auto startoff(rxbuffer.offset());
                if (rxbuffer.avail() < sizeof(proto::respheader)) break;
                deserialise1 ds(rxbuffer);
                proto::respheader hdr(ds);
                if (ds.isfailure()) {
                    _failure = ds.failure();
                    break; }
                if (hdr.size > rxbuffer.avail()) break;
                /* Have whole message -> can deserialise and process
                 * it. */
                auto completed(
                    rxlock.locked<_asynccall *>(
                        [this, &hdr] () -> _asynccall * {
                            for (auto it(rxqueue.start());
                                 !it.finished();
                                 it.next()) {
                                if ((*it)->seqnr == hdr.seq) {
                                    auto r(*it);
                                    it.remove();
                                    return r; } }
                            return NULL; } ) );
                if (completed != NULL) {
                    if (hdr.status.isfailure()) {
                        completed->fail(
                            hdr.status.failure(),
                            onconnectionthread()); }
                    else {
                        completed->complete(
                            ds,
                            onconnectionthread()); }
                    if (ds.offset() != startoff + hdr.size) {
                        logmsg(loglevel::error,
                               "expected reply to run from " +
                               fields::mk(startoff) + " to " +
                               fields::mk(startoff + hdr.size) +
                               "; actually went to " +
                               fields::mk(ds.offset()));
                        _failure = error::invalidmessage; } }
                else {
                    logmsg(
                        loglevel::debug,
                        "peer completed call after it was locally aborted"); }
                rxbuffer.discard(hdr.size); }
            insub.rearm(); }
        else if (ss == &shutdownsub) {
            if (shutdown.ready()) _failure = error::shutdown; }
        else if (hellocall.isjust() && ss == &hellosub.just()) {
            auto token(hellocall.just()->finished());
            if (token == Nothing) continue;
            hellosub = Nothing;
            auto res(hellocall.just()->pop(token.just()));
            hellocall = Nothing;
            connectlock.locked([this, res] {
                    assert(connectres == Nothing);
                    connectres = res; });
            connectpub.publish(); }
        else abort(); }

    /* If we haven't connected yet then we certainly won't now. */
    if (connectres == Nothing) {
        connectlock.locked([this, _failure] {
                connectres = _failure.failure(); });
        connectpub.publish(); }

    /* No longer processing messages.  Stop anyone making further
     * calls. */
    rxlock.locked([this] { rxstopped = true; });

    /* Abort any calls already in progress.  Don't need the lock for
     * this because rxstopped is set and that stops any other threads
     * playing with the list. */
    for (auto it(rxqueue.start()); !it.finished(); it.next()) {
        (*it)->fail(_failure.failure(), onconnectionthread()); }

    /* Stop fast TX.  Slow TX is already stopped because we're the
     * only thread which can do it.  */
    int _fd = fd;
    txlock.locked([this] { fd = -1; });

    /* Close the socket. */
    ::close(_fd);

    /* We're done. */ }

rpcclient2::~rpcclient2() {
    auto w(worker());
    w->shutdown.set();
    /* Worker thread always shuts down quickly once shutdown is
     * set. */
    w->join(clientio::CLIENTIO); }
