#include "rpcclient.H"

#include <sys/socket.h>
#include <unistd.h>

#include "fields.H"
#include "peername.H"
#include "proto.H"
#include "thread.H"
#include "util.H"
#include "version.H"
#include "wireproto.H"

#include "either.tmpl"
#include "list.tmpl"
#include "maybe.tmpl"
#include "mutex.tmpl"
#include "thread.tmpl"

class rpcclient::workerthread : public thread {
public:  rpcclient::asyncconnect connector;
public:  const peername peer;
    /* Protects newcalls and failure.  Small leaf lock. */
private: mutex_t newcallsmux;
    /* List of calls which have not yet been sent to the peer.
     * Protected by newcallsmux.  newcallspub should be published
     * whenever this goes from empty to non-empty. */
private: list<asynccall *> newcalls;
    /* Notified shortly after newcalls becomes nonempty. */
private: publisher newcallspub;
    /* Set once the connection has encountered a fatal error.  If this
     * is set then no more calls should be added to newcalls.
     * Protected by newcallsmux.  Updated only by the connection
     * thread. */
private: orerror<void> failure;
    /* Set when it's time to shut the connection down. */
public:  waitbox<void> shutdown;
    /* Protects the sequence number allocator. */
private: mutex_t seqlock;
    /* Sequence number allocator.  Protected by seqlock.*/
private: wireproto::sequencer sequencer;
    /* Slightly skanky: the rpcclient itself is just a proxy for the
     * workerthread with every so slightly different lifetime rules. */
public:  rpcclient *const client;

public:  workerthread(const constoken &t,
                      const peername &_peer);
public:  void run(clientio);
private: orerror<int> connect(clientio);
public:  rpcclient::asynccall *call(const wireproto::req_message &m);
};

orerror<rpcclient *>
rpcclient::connect(
    clientio io,
    const peername &peer,
    maybe<timestamp> deadline) {
    auto a(connect(peer));
    maybe<asyncconnect::token> t(Nothing);
    {   subscriber sub;
        subscription ss(sub, a->pub());
        while (true) {
            t = a->finished();
            if (t != Nothing) break;
            auto s(sub.wait(io, deadline));
            if (s == NULL) return error::timeout;
            assert(s == &ss); } }
    return a->pop(t.just()); }

rpcclient *
rpcclient::asyncconnect::client() const {
    return containerof(this, rpcclient::workerthread, connector)->client; }

rpcclient::asyncconnect::asyncconnect()
    : mux(),
      _pub(),
      res(Nothing) {}

rpcclient::asyncconnect::token::token() {}

maybe<rpcclient::asyncconnect::token>
rpcclient::asyncconnect::finished() const {
    return mux.locked<maybe<token> >([this] (mutex_t::token) -> maybe<token> {
            if (res == Nothing) return Nothing;
            else return token(); }); }

const publisher &
rpcclient::asyncconnect::pub() const { return _pub; }

orerror<rpcclient *>
rpcclient::asyncconnect::pop(rpcclient::asyncconnect::token) {
    /* Because finished() must return non-Nothing. */
    assert(res != Nothing);
    auto lres(res.just());
    /* If the connect failed then we are responsible for tearing the
     * connection down from here. */
    if (lres.isfailure()) {
        delete client(); /* deletes this as well! */
        return lres.failure(); }
    else return client(); }

void
rpcclient::asyncconnect::abort() { delete client(); }

rpcclient::asyncconnect::~asyncconnect() {}

rpcclient::asyncconnect *
rpcclient::connect(const peername &peer) {
    auto p(thread::spawn<workerthread>(
               "C" + fields::mk(peer),
               peer));
    auto w(p.go());
    return &w->connector; }

rpcclient::workerthread::workerthread(const constoken &t,
                                      const peername &_peer)
    : thread(t),
      connector(),
      peer(_peer),
      newcallsmux(),
      newcalls(),
      newcallspub(),
      failure(Success),
      shutdown(),
      seqlock(),
      sequencer(),
      client(new rpcclient(this)) {}

orerror<int>
rpcclient::workerthread::connect(clientio io) {
    auto fd(::socket(peer.sockaddr()->sa_family,
                     SOCK_STREAM | SOCK_NONBLOCK,
                     0));
    if (fd < 0) return error::from_errno();
    if (::connect(fd, peer.sockaddr(), peer.sockaddrsize()) < 0 &&
        errno != EINPROGRESS) {
        ::close(fd);
        return error::from_errno(); }

    subscriber sub;
    subscription sssub(sub, shutdown.pub);
    iosubscription completesub(sub, fd_t(fd).poll(POLLOUT));
    orerror<int> res(fd);
    while (true) {
        if (shutdown.ready()) {
            res = error::shutdown;
            break; }
        auto ss(sub.wait(io));
        if (ss == &sssub) continue;
        assert(ss == &completesub);
        /* Check for spurious events on the iosub.  Never actually
         * happens, because of implementation details, but it's
         * allowed by the API, so handle it. */
        auto pfd(fd_t(fd).poll(POLLOUT));
        if (::poll(&pfd, 1, 0) < 0) {
            res = error::from_errno();
            break; }
        if (pfd.revents & POLLOUT) break; }
    if (res.issuccess()) {
        int err;
        socklen_t sz(sizeof(err));
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &sz) < 0) {
            res = error::from_errno();
            res.failure()
                .warn("getting connect error for " + fields::mk(peer)); }
        else if (err != 0) res = error::from_errno(err); }
    if (res.isfailure()) ::close(fd);
    return res; }

void
rpcclient::workerthread::run(clientio io) {
    int fd;
    {   auto r(connect(io));
        if (r.isfailure()) {
            connector.mux.locked([r, this] (mutex_t::token) {
                    assert(connector.res == Nothing);
                    connector.res = r.failure(); });
            connector._pub.publish();
            return; }
        fd = r.success(); }
    subscriber sub;
    subscription shutdownsub(sub, shutdown.pub);
    subscription newcallssub(sub, newcallspub);
    maybe<iosubscription> outsub(Nothing);
    maybe<iosubscription> insub(Nothing);
    list<asynccall *> pendingsend;
    list<asynccall *> pendingrecv;
    orerror<void> _failure(Success);
    buffer inbuffer;
    if (shutdown.ready()) _failure = error::shutdown;
    auto hellocall(call(wireproto::req_message(proto::HELLO::tag)
                        .addparam(proto::HELLO::req::version,
                                  version::current)));
    maybe<subscription> hellodone(Nothing);
    hellodone.mkjust(sub, hellocall->pub());
    /* XXX when the connection is idle we don't have any IO
     * subscriptions on it, so won't notice if it dies.  Is that a
     * problem? */
    while (_failure == Success) {
        auto ss(sub.wait(io));
        if (ss == &shutdownsub) {
            if (shutdown.ready()) _failure = error::shutdown; }
        else if (hellodone != Nothing && ss == &hellodone.just()) {
            assert(hellocall != NULL);
            auto t(hellocall->finished());
            if (t == Nothing) continue;
            hellodone = Nothing;
            auto r(hellocall->pop(t.just()));
            hellocall = NULL;
            connector.mux.locked([r, this] (mutex_t::token) {
                    assert(connector.res == Nothing);
                    if (r.isfailure()) connector.res = r.failure();
                    else connector.res = Success; });
            connector._pub.publish();
            if (r.isfailure()) _failure = r.failure();
            else delete r.success(); }
        else if (ss == &newcallssub) {
            bool sendempty = pendingsend.empty();
            newcallsmux.locked([&pendingsend, this] (mutex_t::token) {
                    pendingsend.transfer(newcalls); });
            if (sendempty && outsub == Nothing && !pendingsend.empty()) {
                /* XXX missing an obvious optimisation here */
                outsub.mkjust(sub, fd_t(fd).poll(POLLOUT)); } }
        else if (outsub != Nothing && ss == &outsub.just()) {
            /* outsub is only set when we have something to send. */
            assert(!pendingsend.empty());
            bool recvempty = pendingrecv.empty();
            for (auto it(pendingsend.start()); !it.finished(); ) {
                auto p(*it);
                assert(!p->outbuf.empty());
                /* Check whether the call's been aborted before we
                 * send it. */
                /* (It might, of course, get aborted after we send it.
                 * That's fine: asynchronous aborts are always
                 * best-effort.) */
                if (p->mux.locked<bool>(
                        [p] (mutex_t::token) {
                            if (p->res == Nothing) return false;
                            assert(p->res.just().isleft());
                            return true; })) {
                    delete p;
                    it.remove();
                    continue; }
                auto r(p->outbuf.sendfast(fd_t(fd)));
                if (r.issuccess()) {
                    /* XXX relying on Nagle's algorithm to combine
                     * small calls.  Might be better off merging them
                     * in userspace?  Not sure how worthwhile that
                     * would be; the common case is that we do one at
                     * a time, anyway.*/
                    if (p->outbuf.empty()) {
                        pendingrecv.pushtail(p);
                        it.remove(); } }
                else {
                    if (r == error::wouldblock) {
                        /* Socket TX buffer is full.  Go back to
                         * waiting. */ }
                    else {
                        r.failure().warn("sending to " + fields::mk(peer));
                        _failure = r.failure(); }
                    break; } }
            if (pendingsend.empty()) outsub = Nothing;
            else outsub.just().rearm();
            if (recvempty && insub == Nothing && !pendingrecv.empty()) {
                insub.mkjust(sub, fd_t(fd).poll(POLLIN)); } }
        else if (insub != Nothing && ss == &insub.just()) {
            auto rxres(inbuffer.receive(io, fd_t(fd)));
            if (rxres.isfailure()) {
                rxres.warn("receiving from " + fields::mk(peer));
                _failure = rxres.failure();
                break; }
            while (!pendingrecv.empty()) {
                auto msg(wireproto::rx_message::fetch(inbuffer));
                if (msg == error::underflowed) break;
                if (msg.isfailure()) {
                    rxres.warn("decoding from " + fields::mk(peer));
                    _failure = msg.failure();
                    break; }
                for (auto it(pendingrecv.start()); !it.finished(); it.next()) {
                    auto p(*it);
                    if (p->sequence != msg.success().sequence()) continue;
                    typedef either<void, orerror<wireproto::rx_message *> >
                        rettype;
                    auto e(msg.success().getparam(wireproto::err_parameter));
                    auto destroy(
                        p->mux.locked<bool>(
                            [e, p, &msg] (mutex_t::token) {
                                if (p->res != Nothing) {
                                    assert(p->res.just().isleft());
                                    return true; }
                                else if (e == Nothing) {
                                    p->res = rettype(msg.success().steal());
                                    return false; }
                                else {
                                    p->res = rettype(e.just());
                                    return false; } }));
                    if (destroy) delete p;
                    else p->_pub.publish();
                    it.remove();
                    break; } }
            if (pendingrecv.empty()) insub = Nothing;
            else insub.just().rearm(); } }
    ::close(fd);
    /* Prevent anyone from starting further calls. */
    newcallsmux.locked([_failure, this] (mutex_t::token) {
            failure = _failure; });
    /* Fail any calls which have already started. */
    newcallsmux.locked([&pendingsend, this] (mutex_t::token) {
            pendingsend.transfer(newcalls); });
    while (!pendingsend.empty() || !pendingrecv.empty()) {
        auto p(pendingsend.empty()
               ? pendingrecv.pophead()
               : pendingsend.pophead());
        if (p->mux.locked<bool>([_failure, p] (mutex_t::token) {
                    if (p->res == Nothing) {
                        p->res = either<void, orerror<wireproto::rx_message *> >
                            (_failure.failure());
                        return false; }
                    else {
                        assert(p->res.just().isleft());
                        return true; } } ) ) {
                delete p; } } }

rpcclient::asynccall *
rpcclient::workerthread::call(const wireproto::req_message &m) {
    auto res(
        new rpcclient::asynccall(
            m,
            seqlock.locked<wireproto::sequencenr>(
                [this] (mutex_t::token) { return sequencer.get(); })));
    newcallsmux.locked(
        [res, this] (mutex_t::token) {
            if (failure == Success) {
                bool notify(newcalls.empty());
                newcalls.pushtail(res);
                if (notify) newcallspub.publish(); }
            else {
                res->res = either<void, orerror<wireproto::rx_message*> >(
                    failure.failure()); } });
    return res; }

rpcclient::rpcclient(workerthread *_worker)
    : worker(_worker) {}

orerror<wireproto::rx_message *>
rpcclient::call(clientio io,
                const wireproto::req_message &req,
                maybe<timestamp> deadline) {
    auto c(call(req));
    maybe<asynccall::token> r(Nothing);
    {   subscriber sub;
        subscription ss(sub, c->pub());
        while (r == Nothing) {
            r = c->finished();
            if (r != Nothing) break;
            auto s(sub.wait(io, deadline));
            if (s == NULL) break;
            assert(s == &ss); } }
    if (r.isjust()) return c->pop(r.just());
    else {
        c->abort();
        return error::timeout; } }

rpcclient::asynccall::asynccall(
    const wireproto::req_message &m,
    wireproto::sequencenr snr)
    : mux(),
      res(Nothing),
      _pub(),
      outbuf(),
      sequence(snr.reply()) {
    m.serialise(outbuf, snr); }

rpcclient::asynccall::token::token() {}

maybe<rpcclient::asynccall::token>
rpcclient::asynccall::finished() const {
    return mux.locked<maybe<token> >(
        [this] (mutex_t::token) -> maybe<token> {
            if (res == Nothing) return Nothing;
            else return token(); }); }

const publisher &
rpcclient::asynccall::pub() const { return _pub; }

orerror<wireproto::rx_message *>
rpcclient::asynccall::pop(token) {
    auto lres(res);
    assert(lres != Nothing);
    assert(lres.just().isright());
    delete this;
    return lres.just().right(); }

void
rpcclient::asynccall::abort() {
    auto t(mux.lock());
    if (res == Nothing) {
        res = either<void, orerror<wireproto::rx_message*> >();
        mux.unlock(&t); }
    else {
        mux.unlock(&t);
        if (res.just().right().issuccess()) delete res.just().right().success();
        delete this; } }

rpcclient::asynccall::~asynccall() {}

rpcclient::asynccall *
rpcclient::call(const wireproto::req_message &m) {
    return worker->call(m); }

rpcclient::~rpcclient() {
    worker->shutdown.set();
    /* The worker thread is guaranteed to shut down quickly once
     * shutdown is set, so we don't need a clientio token. */
    worker->join(clientio::CLIENTIO); }
