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
#include "test.H"
#include "thread.H"
#include "util.H"
#include "version.H"

#include "list.tmpl"
#include "mutex.tmpl"
#include "rpcclient2.tmpl"
#include "test.tmpl"
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
    /* What interface do we need them to expose? */
public: const interfacetype type;
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
public: publisher abortedpub;

    /* Shutdown machine. */
public: waitbox<void> shutdown;

public: workerthread(const constoken &tok, interfacetype, const peername &p);

public: void run(clientio);

    /* Simple wrapper aroudn getsockopt(SO_ERROR) */
public: static orerror<void> socketerr(int fd, const peername &peer);
public: orerror<int> connect(clientio io); };

nnp<rpcclient2::workerthread>
rpcclient2::worker() const { return *containerof(this, workerthread, api); }

/* Allocate a sequence number for a call and put it on the pending-RX
 * list.  The call can't be on the RX list already.  Returns
 * error::shutdown if we've already started a shutdown sequence. */
orerror<void>
rpcclient2::queuerx(nnp<_asynccall> cll) {
    auto w((worker()));
    return w->rxlock.locked<orerror<void> >(
        [cll, w] () -> orerror<void> {
            if (w->rxstopped) return error::shutdown;
            cll->seqnr = w->nextseqnr;
            w->nextseqnr++;
            w->rxqueue.pushtail(cll);
            return Success; } ); }

/* Queue something for transmit.  This can sometimes race with a
 * shutdown sequence, if the other side goes away while we're working,
 * in which case the stuff still goes in the TX queue but will never
 * be sent.  Cannot race with the local side being torn down. */
void
rpcclient2::queuetx(
    const std::function<void (serialise1 &, mutex_t::token)> &serialise,
    proto::sequencenr seqnr) {
    auto w((worker()));
    auto notify(
        w->txlock.locked<bool>(
            [seqnr, &serialise, w]
            (mutex_t::token txtoken) {
                auto startavail(w->txbuffer.avail());
                serialise1 s(w->txbuffer);
                /* size gets filled in later */
                proto::reqheader(-1, version::current, seqnr).serialise(s);
                serialise(s, txtoken);
                /* Set size in header. */
                auto sz = w->txbuffer.avail() - startavail;
                assert(sz < proto::maxmsgsize);
                *w->txbuffer.linearise<unsigned>(
                    startavail + w->txbuffer.offset()) = (unsigned)sz;
                /* If there was stuff in the buffer before we started
                 * then the worker thread is already live and there's
                 * no need to wake it. */
                if (startavail != 0) return false;
                /* Try a fast synchronous send before waking the
                 * worker. */
                if (w->fd >= 0) (void)w->txbuffer.sendfast(fd_t(w->fd));
                /* Wake worker if and only if that didn't clear the
                 * backlog. */
                return !w->txbuffer.empty(); } ) );
    if (notify) w->txgrew.publish(); }

publisher &
rpcclient2::abortedpub() { return worker()->abortedpub; }

rpcclient2::_asynccall::_asynccall(publisher &_abortedpub)
    : mux(),
      aborted(false),
      _finished(false),
      _pub(),
      abortedpub(_abortedpub),
      seqnr(99 /* filled in later */) {}

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

rpcclient2::_asynccall::~_asynccall() { assert(aborted || _finished); }

rpcclient2::rpcclient2() {}

nnp<rpcclient2::asyncconnect>
rpcclient2::connect(interfacetype type, const peername &pn) {
    return thread::start<workerthread>("C:" + fields::mk(pn), type, pn)
        ->connector; }

nnp<rpcclient2::workerthread>
rpcclient2::asyncconnect::owner() const {
    return *containerof(this, workerthread, connector); }

rpcclient2::asyncconnect::asyncconnect() {}

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
        owner()->api.destroy();
        return res; }
    else return _nnp(owner()->api); }

void
rpcclient2::asyncconnect::abort() { owner()->api.destroy(); }

rpcclient2::asyncconnect::~asyncconnect() {}

orerror<nnp<rpcclient2> >
rpcclient2::connect(
    clientio io,
    interfacetype type,
    const peername &pn,
    maybe<timestamp> deadline) {
    auto ac(connect(type, pn));
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

rpcclient2::onconnectionthread::onconnectionthread() {}

void
rpcclient2::destroy() {
    worker()->shutdown.set();
    /* Worker thread always shuts down quickly once shutdown is
     * set. */
    worker()->join(clientio::CLIENTIO); }

rpcclient2::~rpcclient2() {}

tests::hookpoint<void>
rpcclient2::doneconnectsyscall([] { } );

rpcclient2::workerthread::workerthread(
    const constoken &token,
    interfacetype _type,
    const peername &_peer)
    : thread(token),
      api(),
      connector(),
      connectlock(),
      connectres(Nothing),
      connectpub(),
      type(_type),
      peer(_peer),
      rxlock(),
      rxqueue(),
      rxstopped(false),
      /* Something recognisable and large enough to flush out 32 bit
       * truncation bugs. */
      nextseqnr(0x156782345ul),
      txlock(),
      txbuffer(),
      txgrew(),
      fd(-1),
      abortedpub(),
      shutdown() {}

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

    rpcclient2::doneconnectsyscall();

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

    /* And for things getting aborted. */
    subscription aborts(sub, abortedpub);
    aborts.set();

    /* Start by sending a HELLO call. */
    maybe<nnp<asynccall<void> > > hellocall(
        api.call<void>(
            [] (serialise1 &s, mutex_t::token) {
                proto::hello::req().serialise(s); },
            [this]
            (asynccall<void> &,
             orerror<nnp<deserialise1> > d,
             onconnectionthread) -> orerror<void> {
                if (d.isfailure()) return d.failure();
                auto &ds(*d.success());
                proto::hello::resp r(ds);
                if (ds.isfailure()) return ds.failure();
                if (r.min > version::current || r.max < version::current) {
                    return error::badversion; }
                if (r.type != type) return error::invalidparameter;
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
            assert(outsubarmed);
            outsubarmed = false;
            if (!txbuffer.empty()) {
                auto res(txlock.locked<orerror<void> >([this] {
                            return txbuffer.sendfast(fd_t(fd)); }) );
                if (res.isfailure() && res != error::wouldblock) {
                    _failure = res.failure(); } }
            if (!txbuffer.empty()) {
                outsub.rearm();
                outsubarmed = true; } }
        else if (ss == &insub) {
            auto res(rxbuffer.receivefast(fd_t(fd)));
            insub.rearm();
            if (res.isfailure()) {
                if (res != error::wouldblock) _failure = res.failure();
                continue; }
            while (true) {
                auto startoff(rxbuffer.offset());
                deserialise1 ds(rxbuffer);
                proto::respheader hdr(ds);
                if (ds.isfailure()) {
                    if (ds.failure() != error::underflowed) {
                        _failure = ds.failure(); }
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
                rxbuffer.discard(hdr.size); } }
        else if (ss == &shutdownsub) {
            if (shutdown.ready()) _failure = error::shutdown; }
        else if (hellocall.isjust() && ss == &hellosub.just()) {
            auto token(hellocall.just()->finished());
            if (token == Nothing) continue;
            hellosub = Nothing;
            _failure = hellocall.just()->pop(token.just());
            hellocall = Nothing;
            connectlock.locked([this, _failure] {
                    assert(connectres == Nothing);
                    connectres = _failure; });
            connectpub.publish(); }
        else if (ss == &aborts) {
            /* Go and GC anything which has been aborted. */
            list<nnp<_asynccall> > aborted;
            rxlock.locked(
                [this, &aborted] {
                    bool r;
                    for (auto it(rxqueue.start());
                         !it.finished();
                         r ? it.remove() : it.next()) {
                        auto c(*it);
                        r = c->aborted;
                        if (c->aborted) aborted.pushtail(c); } } );
            for (auto it(aborted.start()); !it.finished(); it.next()) {
                auto c(*it);
                /* Acquire and release lock as a kind of barrier to
                 * make sure that the call's abort() really has
                 * finished with it. */
                c->mux.locked([c] { assert(c->aborted); });
                delete *it; } }
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
