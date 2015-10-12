#include "connpool.H"

#include <sys/socket.h>
#include <unistd.h>

#include "beaconclient.H"
#include "buffer.H"
#include "fields.H"
#include "logging.H"
#include "pair.H"
#include "peername.H"
#include "proto2.H"
#include "pubsub.H"
#include "agentname.H"
#include "socket.H"
#include "thread.H"
#include "util.H"
#include "waitbox.H"

#include "connpool.tmpl"
#include "either.tmpl"
#include "list.tmpl"
#include "mutex.tmpl"
#include "orerror.tmpl"
#include "pair.tmpl"
#include "test.tmpl"
#include "thread.tmpl"

/* Because I got bored typing the same thing over and over again. */
#define POOL connpool::impl
#define CALL connpool::asynccall::impl
#define CONN POOL::conn

tests::hookpoint<void> connpool::reapedconnthread([] {});

/* The pool thread itself is responsible for establishing and
 * destroying connections to remote machines when appropriate. */
class POOL : public thread {
    /* We run a connection for each peer we're currently keeping track
     * of.  All of the connections are expected to watch the owner
     * shutdown box and shut down quickly once it's set. */
public: class conn;

public: const config cfg;

    /* Embed the API structure to give callers outside of this file a
     * nicer API. */
public: connpool api;
    /* Set once someone calls api::destroy() on the pool. */
public: waitbox<void> shutdown;

    /* Beacon client to use for agentname -> peername lookups. */
public: ::beaconclient &beacon;

public: mutex_t mux;

    /* All of the currently-live conns.  Protected by the mux. */
public: list<nnp<conn> > _connections;
public: list<nnp<conn> > &connections(mutex_t::token) { return _connections; }

    /* Subscriber for the main pool.  New threads conn get attached to
     * this by their embedded subscription structure; the data on
     * those subscriptions is the conn itself.  Also tracks shutdown
     * with a NULL data. */
public: subscriber sub;

public: explicit impl(const constoken &t, const config &, nnp< ::beaconclient>);
public: ~impl();

public: void run(clientio);

    /* Implementations of our public API, proxied through api. */
public: nnp<connpool::asynccall> call(const agentname &sn,
                                      interfacetype type,
                                      maybe<timestamp> deadline,
                                      const std::function<serialiser> &s,
                                      const deserialiser &ds); };

class CONN : public thread {
public: POOL &pool;
public: mutex_t mux;
    /* Connects the pool subscriber to the thread death publisher. */
public: subscription deathsub;
    /* To whom are we supposed to be connected? */
public: const agentname agent;

    /* Set once the connection is far enough through its shutdown
     * sequence that it can't accept more calls.  dying == true should
     * imply newcalls is empty but doesn't say anything about the
     * aborted or active calls list. */
public: bool _dying;
public: bool &dying(mutex_t::token) { return _dying; }
    /* conn thread can read dying without the lock. */
public: bool dying(connlock) const { return _dying; }

    /* Calls which have been made against the call() interface but not
     * yet picked up by the connection thread. */
public: list<nnp<CALL> > _newcalls;
public: list<nnp<CALL> > &newcalls(mutex_t::token) { return _newcalls; }

    /* Calls which have been abort()ed at the API but not yet released
     * by the connection thread. */
public: list<nnp<CALL> > _aborted;
public: list<nnp<CALL> > &aborted(mutex_t::token) { return _aborted; }

    /* Notified whenever something is added to the aborted or newcalls
     * lists. */
public: publisher callschanged;

    /* true if it's time to shut down: dying set and the call list
     * empty. */
public: bool finished(const list<nnp<CALL> > &, connlock cl) const;

    /* Cause a single call to fail with a given error.  Note that this
     * does not update the calls list. */
public: void failcall(nnp<CALL> what, error err, connlock cl) const;

    /* Cause every call in the list to fail with the same error.
     * Drains the list as it does so. */
public: void harderror(list<nnp<CALL> > &what, error err, connlock cl);

    /* Find the next timeout (which is either the first call timeout,
     * if we have any calls outstanding, or the idle timeout, if we
     * don't).  It's important not to adjust the live calls list
     * between calling this and using the result, because otherwise
     * you'll wait for the wrong thing.  Returns Nothing if we don't
     * have any outstanding timeouts (which can only happen if there's
     * a call outstanding (so that we don't have an idle timeout) and
     * that call has no timeout itself). */
    /* This also does a lot of other interesting work: processing call
     * timeouts, maintaining idledat, processing call aborts, and, if
     * we're not connected, pull new calls into the call list.  */
    /* txbuffer should be non-NULL iff connected is true. */
public: maybe<timestamp> checktimeouts(list<nnp<CALL> > &calls,
                                       connlock cl,
                                       maybe<timestamp> &idledat,
                                       bool connected,
                                       buffer *txbuffer);

    /* Simple wrapper around getsockopt(SO_ERROR) */
public: orerror<void> socketerr(int sock, const peername &_peer);

    /* Serialise a list of calls into the TX buffer, draining
     * the list as we do so. */
public: void queuetx(list<nnp<CALL> > &calls,
                     buffer &txbuffer,
                     proto::sequencenr &nextseq,
                     connlock cl);

    /* Process the next response in the rxbuffer, removing it from the
     * buffer as we do so.  This can complete and remove at most one
     * member of calls as it's working.  Returns error::underflowed if
     * the buffer doesn't yet contain a whole response or some other
     * error for a protocol issue.  Note that application-level errors
     * do not cause processresponse to return an error; errors here
     * indicate a problem with the protocol and can only be recovered
     * from by tearing down and rebuilding the connection. */
public: orerror<void> processresponse(buffer &rxbuffer,
                                      list<nnp<CALL> > &calls,
                                      connlock cl);

    /* Do the agentname->peername translation, the connect(), and the
     * hello. */
public: maybe<fd_t> connectphase(
    clientio io,
    subscriber &sub,
    maybe<pair<peername, timestamp> > &debounceconnect,
    list<nnp<CALL> > &calls,
    connlock cl,
    maybe<timestamp> &idledat,
    proto::sequencenr &nextseqnr);

public: void workphase(
    clientio io,
    list<nnp<CALL> > &calls,
    subscriber &sub,
    fd_t fd,
    const subscription &shutdownsub,
    const subscription &newcallssub,
    proto::sequencenr &nextseqnr,
    maybe<timestamp> &idledat,
    connlock cl);

public: void delayconnect(
    maybe<pair<peername, timestamp> > &debounceconnect,
    const peername &peer);

    /* Queue a call against the conn.  This will take over the entire
     * call machine until it completes.  Only safe if dying is false,
     * so since dying is protected by the mux this can only be called
     * under the mux. */
public: nnp<CALL> call(maybe<timestamp>,
                       interfacetype type,
                       const std::function<serialiser> &,
                       const deserialiser &,
                       mutex_t::token);

    /* We start off with the lock held, to stop the connection dying
     * before we get a chance to use it, so this gives back a mutex
     * token as well as constructing the conn. */
public: conn(const constoken &,
             const agentname &,
             POOL &,
             maybe<mutex_t::token> *);
public: void run(clientio io); };


/* Note that these can survive after the connection drops (and in fact
 * after the whole pool is destroyed) if our users are slow about
 * calling abort() and pop().  That implies that most of the
 * communication should be conn->call, rather than vice-versa. */
class CALL {
public:  connpool::asynccall api;
public:  CONN &conn;

public:  const maybe<timestamp> deadline;
public:  const interfacetype type;
public:  const std::function<connpool::serialiser> serialiser;
public:  const deserialiser deserialise;

    /* Notified when the call completes. */
public:  publisher pub;

    /* Acquired from finished(), which is const. */
public:  mutable mutex_t mux;

public:  maybe<orerror<void> > _res;
public:  maybe<orerror<void> > &res(mutex_t::token) { return _res; }
    /* A token is enough to read _res without the lock, but not to
     * write it. */
public:  maybe<orerror<void> > res(token) const { return _res; }

public:  bool _aborted;
public:  bool &aborted(mutex_t::token) { return _aborted; }

public:  unsigned _refcount;
public:  void reference() { atomicinc(_refcount); }
public:  void dereference() {
    if (atomicloaddec(_refcount) == 1) delete this; }
private: ~impl() {}

    /* Sequence number is only allocated when we transmit, and is
     * reset if we abandon an attempt to do the call. */
public:  maybe<proto::sequencenr> _seqnr;
public:  maybe<proto::sequencenr> &seqnr(connlock) { return _seqnr; }

public:  impl(CONN &_conn,
              maybe<timestamp> _deadline,
              interfacetype _type,
              const std::function<connpool::serialiser> &_serialiser,
              const deserialiser &_deserialise);

    /* Implementations of the public API. */
public:  maybe<token> finished() const;
public:  token finished(clientio) const;
public:  orerror<void> pop(token);
public:  orerror<void> abort(); };

/* ----------------------------- config type ---------------------------- */
connpool::config::config(const beaconclientconfig &_beacon,
                         timedelta _idletimeout,
                         timedelta _connecttimeout,
                         timedelta _hellotimeout,
                         timedelta _debounceconnect)
    : beacon(_beacon),
      idletimeout(_idletimeout),
      connecttimeout(_connecttimeout),
      hellotimeout(_hellotimeout),
      debounceconnect(_debounceconnect) {}

connpool::config::config(const beaconclientconfig &_beacon)
    : beacon(_beacon),
      idletimeout(timedelta::seconds(60)),
      connecttimeout(timedelta::seconds(10)),
      hellotimeout(timedelta::seconds(10)),
      debounceconnect(timedelta::seconds(1)) {}

orerror<connpool::config>
connpool::config::mk(const beaconclientconfig &bc,
                     timedelta idletimeout,
                     timedelta connecttimeout,
                     timedelta hellotimeout,
                     timedelta debounceconnect) {
    if (idletimeout < timedelta::seconds(0) ||
        connecttimeout < timedelta::seconds(0) ||
        hellotimeout < timedelta::seconds(0) ||
        debounceconnect < timedelta::seconds(0)) {
        return error::invalidparameter; }
    else return config(bc,
                       idletimeout,
                       connecttimeout,
                       hellotimeout,
                       debounceconnect); }

connpool::config
connpool::config::dflt(const clustername &cn) {
    /* Can't fail with all defaults. */
    return mk(beaconclientconfig(cn)).success(); }

bool
connpool::config::operator==(const config &o) const {
    return beacon == o.beacon &&
        idletimeout == o.idletimeout &&
        connecttimeout == o.connecttimeout &&
        hellotimeout == o.hellotimeout &&
        debounceconnect == o.debounceconnect; }

/* -------------------------- connpool API proxies ---------------------- */
POOL &
connpool::implementation() { return *containerof(this, impl, api); }

const POOL &
connpool::implementation() const { return *containerof(this, impl, api); }

const connpool::config &
connpool::getconfig() const { return implementation().cfg; }

template <> nnp<connpool::asynccallT<void> >
connpool::_call(const agentname &sn,
                interfacetype type,
                maybe<timestamp> deadline,
                const std::function<connpool::serialiser> &s,
                const deserialiserT<void> &ds) {
    return implementation().call(sn, type, deadline, s, ds); }


orerror<nnp<connpool> >
connpool::build(const config &cfg) {
    auto bc(beaconclient::build(cfg.beacon));
    if (bc.isfailure()) return bc.failure();
    else return _nnp(thread::start<impl>(fields::mk("connpool"),
                                         cfg,
                                         _nnp(*bc.success()))->api); }

orerror<nnp<connpool> >
connpool::build(const clustername &cn) { return build(config::dflt(cn)); }

const beaconclient &
connpool::beaconclient() const { return implementation().beacon; }

void
connpool::destroy() {
    auto i(&implementation());
    i->shutdown.set();
    /* Pool thread always stops quickly once shutdown is set. */
    i->join(clientio::CLIENTIO); }

connpool::~connpool() {}

/* ------------------------ asynccall API proxies ----------------------- */
CALL &
connpool::asynccall::implementation() { return *containerof(this, impl, api); }

const CALL &
connpool::asynccall::implementation() const {
    return *containerof(this, impl, api); }

connpool::asynccall::token::token() {}

maybe<connpool::asynccall::token>
connpool::asynccall::finished() const { return implementation().finished(); }

const publisher &
connpool::asynccall::pub() const { return implementation().pub; }

orerror<void>
connpool::asynccall::pop(token t) { return implementation().pop(t); }

orerror<void>
connpool::asynccall::abort() { return implementation().abort(); }

connpool::asynccall::token
connpool::asynccall::finished(clientio io) const {
    return implementation().finished(io); }

connpool::asynccall::~asynccall() {}

/* ----------------------- connpool implementation --------------------- */
POOL::impl(const constoken &t,
           const config &_cfg,
           nnp< ::beaconclient> _beacon)
    : thread(t),
      cfg(_cfg),
      api(),
      shutdown(),
      beacon(_beacon),
      mux(),
      _connections(),
      sub() {}

POOL::~impl() { beacon.destroy(); }

void
POOL::run(clientio io) {
    subscription ss(sub, shutdown.pub());
    while (!shutdown.ready()) {
        auto s(sub.wait(io));
        if (s == &ss) continue;
        auto c = (conn *)s->data;
        auto t(c->hasdied());
        if (t == Nothing) continue;
        logmsg(loglevel::debug, "reaping " + fields::mk(c->agent));
        /* Thread must declare itself to be dying before exiting (and
         * that's necessary for us to get away with not taking the
         * connection lock here). */
        assert(c->_dying);
        mux.locked([this, c] (mutex_t::token tok) {
                for (auto it(connections(tok).start());
                     true;
                     it.next()) {
                    if (*it == c) {
                        it.remove();
                        return; } } });
        /* Thread should have cleared all calls before shutting
         * down. */
        assert(c->_newcalls.empty());
        assert(c->_aborted.empty());
        connpool::reapedconnthread();
        c->join(t.just()); }
    /* The shutdown box is set so all of our connections should be
     * shutting down.  Wait for them to do so. */
    auto token(mux.lock());
    while (!connections(token).empty()) {
        auto c(connections(token).pophead());
        /* Not clear whether dropping the lock here is *necessary*,
         * but it is safe, and it makes things easier to think about,
         * so do it anyway. */
        mux.unlock(&token);
        c->join(clientio::CLIENTIO);
        token = mux.lock(); }
    mux.unlock(&token); }

nnp<connpool::asynccall>
POOL::call(const agentname &sn,
           interfacetype type,
           maybe<timestamp> deadline,
           const std::function<connpool::serialiser> &s,
           const deserialiser &ds) {
    assert(!shutdown.ready());
    auto token(mux.lock());
    maybe<pair<nnp<CONN>, mutex_t::token> > worker(Nothing);
    for (auto it(connections(token).start());
         worker == Nothing && !it.finished();
         it.next()) {
        auto cc(*it);
        if (cc->agent != sn) continue;
        auto workertoken(cc->mux.lock());
        /* Don't take connections which are already dying. */
        if (cc->dying(workertoken)) cc->mux.unlock(&workertoken);
        else worker = mkpair(_nnp(*cc), workertoken); }
    if (worker == Nothing) {
        /* No existing worker -> start one. */
        maybe<mutex_t::token> tok(Nothing);
        auto w(thread::start<conn>(
                   "C:" + fields::mk(sn),
                   sn,
                   *this,
                   &tok));
        assert(tok != Nothing);
        /* Dying starts off clear, and the lock has never been
         * released, so it can't be set now. */
        assert(!w->dying(tok.just()));
        worker = mkpair(_nnp(*w), tok.just());
        connections(token).pushtail(_nnp(*w)); }
    /* Have a connection with dying clear and the conn lock is
     * sufficient to stop dying being set -> no longer need the pool
     * lock. */
    mux.unlock(&token);
    auto &w(worker.just());
    auto res(_nnp(w.first()->call(deadline, type, s, ds, w.second())->api));
    /* Conn now responsible for completing the call -> no longer need
     * the lock. */
    w.first()->mux.unlock(&w.second());
    return res; }

/* ------------------------ Connection implementation ---------------------- */
bool
CONN::finished(const list<nnp<CALL> > &calls, connlock cl) const {
    /* Necessary condition for it to be safe for us to shut down: no
     * calls outstanding, and no more can be added. */
    if (calls.empty() && dying(cl)) {
        /* Can't extend newcalls while dying is set and we're supposed
         * to flush it when we set dying, so it must be empty now. */
        assert(_newcalls.empty());
        return true; }
    else return false; }

void
CONN::failcall(nnp<CALL> what, error err, connlock cl) const {
    auto callres(what->deserialise(what->api, err, cl));
    what->mux.locked([&callres, what] (mutex_t::token tok) {
            assert(what->res(tok) == Nothing);
            what->res(tok) = callres;
            what->pub.publish(); }); }

void
CONN::harderror(list<nnp<CALL> > &calls, error e, connlock cl) {
    for (auto it(calls.start()); !it.finished(); it.remove()) {
        failcall(*it, e, cl); }
    calls.flush(); }

maybe<timestamp>
CONN::checktimeouts(list<nnp<CALL> > &calls,
                    connlock cl,
                    maybe<timestamp> &idledat,
                    bool connected,
                    buffer *txbuffer) {
    assert((txbuffer == NULL) == !connected);
    /* If we're not connected then we pick up incoming calls now. */
    if (!connected) {
        mux.locked([this, &calls] (mutex_t::token tok) {
                calls.transfer(newcalls(tok)); }); }

    bool quick = false;
    list<nnp<CALL> > aborts;
    mux.locked([this, &aborts] (mutex_t::token tok) {
            aborts.transfer(aborted(tok)); });
    for (auto it(aborts.start()); !it.finished(); it.next()) {
        auto c(*it);
        assert(c->_aborted);
        bool found = false;
        for (auto it2(calls.start()); !it2.finished(); it2.next()) {
            if (*it2 == c) {
                found = true;
                if (connected) {
                    /* The message was already sent.  Queue up an
                     * abort. */
                    bool wasempty = txbuffer->empty();
                    auto startoff(txbuffer->offset() + txbuffer->avail());
                    serialise1 s(*txbuffer);
                    proto::reqheader(-1,
                                     version::current,
                                     interfacetype::meta,
                                     c->seqnr(cl).just())
                        .serialise(s);
                    proto::meta::tag::abort.serialise(s);
                    auto sz(txbuffer->offset() + txbuffer->avail() - startoff);
                    assert(sz <= proto::maxmsgsize);
                    *txbuffer->linearise<unsigned>(startoff) = (unsigned)sz;
                    /* Tell caller they have work to do. */
                    quick |= wasempty; }
                it2.remove();
                break; } }
        if (!found) {
            /* This can happen if someone starts a new call and then
             * abort()s it before we pull it out of the newcalls list
             * or if they abort() it after we've finished processing.
             * Check the newcalls list as well. */
            mux.locked([this, c, &found] (mutex_t::token tok) {
                    for (auto it2(newcalls(tok).start());
                         !it2.finished();
                         it2.next()) {
                        if (*it2 == c) {
                            it2.remove();
                            found = true;
                            return; } }
                    /* finished before abort().  Fine. */}); }
        if (found) failcall(c, error::aborted, cl);
        c->dereference(); }
    /* If we queued up an abort then the caller needs to wake up
     * immediately to process it. */
    if (quick) return timestamp::now();
    /* Zap anything which has already timed out. */
    list<nnp<CALL> > timeout;
    for (auto it(calls.start()); !it.finished(); /**/) {
        auto c(*it);
        if (c->deadline != Nothing && c->deadline.just().inpast()) {
            it.remove();
            failcall(c, error::timeout, cl); }
        else it.next(); }
    if (pool.shutdown.ready() && !dying(cl)) {
        /* Told to shut down -> go to dying mode. */
        mux.locked([this] (mutex_t::token tok) {
                dying(tok) = true; });
        /* Can't get more calls once pool shutdown set.  Kill off
         * existing ones. */
        calls.transfer(_newcalls /* No lock: we're dying. */);
        harderror(calls, error::disconnected, cl);
        assert(calls.empty()); }

    /* If we've gone idle then set idledat.  If we've gone non-idle
     * then clear it.  That's not perfect (there might be a lag
     * between idling and setting the time), but it'll be a short lag,
     * so it's probably good enough. */
    if (idledat.isjust() != calls.empty()) {
        if (calls.empty()) idledat = timestamp::now();
        else idledat = Nothing; }
    assert(idledat.isjust() == calls.empty());

    /* If we're currently idle then the only (and therefore next)
     * timeout is the idle timeout. */
    if (idledat.isjust()) {
        if (dying(cl)) {
            /* We're trying to shut down and have no outstanding calls
             * -> return immediately. */
            return timestamp::now(); }
        auto r(idledat.just() + pool.cfg.idletimeout);
        if (r.inpast()) {
            /* Hit idle timeout -> go to dying mode. */
            auto raced(
                mux.locked<bool>(
                    [this] (mutex_t::token tok) {
                        if (!newcalls(tok).empty()) return true;
                        dying(tok) = true;
                        return false; } ) );
            if (raced) {
                /* More calls arrived just in time to stop us timing
                 * out.  Try again from the top. */
                return checktimeouts(calls, cl, idledat, connected, txbuffer);}
            else {
                logmsg(loglevel::debug,
                       "connection to " + fields::mk(agent) + " timed out"); } }
        /* Waiting for idle timeout. */
        return r; }
    /* Otherwise, take the soonest call timeout. */
    maybe<timestamp> res(Nothing);
    res.silencecompiler(timestamp::now());
    for (auto it(calls.start()); !it.finished(); it.next()) {
        if (res == Nothing) res = (*it)->deadline;
        else if ((*it)->deadline != Nothing &&
                 res.just().after((*it)->deadline.just())) {
            res = (*it)->deadline; } }
   return res; }

orerror<void>
CONN::socketerr(int sock, const peername &_peer) {
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

void
CONN::queuetx(list<nnp<CALL> > &calls,
              buffer &txbuffer,
              proto::sequencenr &nextseq,
              connlock cl) {
    serialise1 s(txbuffer);
    bool remove;
    for (auto it(calls.start());
         !it.finished();
         remove ? it.remove() : it.next()) {
        auto c(*it);

        /* Quick lock-free check.  It doesn't matter if this misses
         * some updates; async abort is inherently racy, anyway. */
        if (c->_aborted) {
            failcall(c, error::aborted, cl);
            remove = true;
            continue; }
        /* This is also a good place to check for timeouts. */
        if (c->deadline != Nothing && c->deadline.just().inpast()) {
            failcall(c, error::timeout, cl);
            remove = true;
            continue; }
        /* Otherwise, it's still a viable call and we should add it to
         * the TX buffer. */
        /* XXX should maybe limit size of TX buffer to make timeouts a
         * bit more timely? */
        remove = false;
        if (c->seqnr(cl) == Nothing) {
            c->seqnr(cl) = nextseq;
            nextseq++; }
        auto startoff(txbuffer.offset() + txbuffer.avail());
        proto::reqheader(-1,
                         version::current,
                         c->type,
                         c->seqnr(cl).just())
            .serialise(s);
        c->serialiser(s, cl);
        auto sz(txbuffer.offset() + txbuffer.avail() - startoff);
        assert(sz <= proto::maxmsgsize);
        *txbuffer.linearise<unsigned>(startoff) = (unsigned)sz; } }

orerror<void>
CONN::processresponse(buffer &rxbuffer,
                      list<nnp<CALL> > &calls,
                      connlock cl) {
    if (pool.shutdown.ready()) return error::shutdown;
    deserialise1 ds(rxbuffer);
    proto::respheader hdr(ds);
    if (ds.status() == error::underflowed) return error::underflowed;
    if (hdr.size > proto::maxmsgsize) ds.fail(error::invalidmessage);
    if (ds.isfailure()) {
        ds.failure().warn(
            "parsing response header from " + fields::mk(agent));
        return ds.failure(); }
    if (hdr.size > rxbuffer.avail()) return error::underflowed;

    /* We've got enough in the buffer to process this message */
    CALL *c = NULL;
    for (auto it(calls.start()); !it.finished(); it.next()) {
        assert((*it)->seqnr(cl).isjust());
        if ((*it)->seqnr(cl).just() == hdr.seq) {
            c = *it;
            it.remove();
            break; } }

    if (c == NULL) {
        /* This can sometimes happen if we cancel a call before
         * receiving a reply. */
        logmsg(loglevel::debug,
               "dropping response from " + fields::mk(agent) +
               "; no call"); }
    else {
        orerror<void> callres(Success);
        /* Invoke the deserialise callback without holding the lock. */
        if (hdr.status.issuccess()) {
            callres = c->deserialise(c->api, _nnp(ds), cl); }
        else callres = c->deserialise(c->api, hdr.status.failure(), cl);
        if (callres.issuccess() && ds.isfailure()) {
            /* Really shouldn't be dropping errors here. */
            logmsg(loglevel::error,
                   "call deserialiser for " + agent.field() +
                   " type " + c->type.field() +
                   " reported success even though deserialiser failed with " +
                   ds.failure().field()); }
        c->mux.locked(
            [c, callres] (mutex_t::token tok) {
                assert(c->res(tok) == Nothing);
                /* Once we set res and drop the lock c can be
                 * released underneath us. */
                c->res(tok) = callres;
                c->pub.publish(); }); }
    rxbuffer.discard(hdr.size);
    return Success; }

maybe<fd_t>
CONN::connectphase(
    clientio io,
    subscriber &sub,
    maybe<pair<peername, timestamp> > &debounceconnect,
    list<nnp<CALL> > &calls,
    connlock cl,
    maybe<timestamp> &idledat,
    proto::sequencenr &nextseqnr) {
    /* Not started connecting yet -> find out where we're connecting
     * to. */
    auto _beaconres(pool.beacon.poll(agent));
    if (_beaconres == Nothing) {
        /* Beacon doesn't know where we need to connect to yet -> wait
         * until it does. */
        subscription beaconsub(sub, pool.beacon.changed());
        logmsg(loglevel::debug, "waiting for beacon");
        _beaconres = pool.beacon.poll(agent);
        if (_beaconres == Nothing) {
            sub.wait(io, checktimeouts(calls, cl, idledat, false, NULL));
            return Nothing; } }
    auto &peer(_beaconres.just().name());

    if (debounceconnect.isjust() &&
        debounceconnect.just().first() == peer &&
        debounceconnect.just().second().infuture()) {
        /* Tried to connect recently -> wait a bit before retrying on
         * the same peer. */
        subscription beaconsub(sub, pool.beacon.changed());
        auto deadline(checktimeouts(calls, cl, idledat, false, NULL));
        if (deadline == Nothing ||
            deadline.just().after(debounceconnect.just().second()))
            deadline = debounceconnect.just().second();
        assert(deadline.isjust());
        sub.wait(io, deadline);
        return Nothing; }

    /* Know where we're supposed to be connecting to -> do it. */
    /* XXX this is a lot easier with raw syscalls than with our fancy
     * listenfd type -> listenfd probably needs some work.  Or to just
     * die. */
    auto sock(::socket(peer.sockaddr()->sa_family,
                       SOCK_STREAM | SOCK_NONBLOCK,
                       0));
    if (sock < 0) {
        auto e(error::from_errno());
        e.warn("socket() for " + fields::mk(agent));
        /* Not being able to use the address family is a hard error.
         * Most other connect()-time errors are soft. */
        harderror(calls, e, cl);
        delayconnect(debounceconnect, peer);
        return Nothing; }
    if (::connect(sock, peer.sockaddr(), peer.sockaddrsize()) < 0 &&
        errno != EINPROGRESS) {
        error::from_errno().warn("connect() to " + fields::mk(agent));
        ::close(sock);
        delayconnect(debounceconnect, peer);
        return Nothing; }

    {   auto r(socket_t(sock).setsockoptions());
        if (r.isfailure()) {
            r.failure().warn("setting sock options for connection");
            ::close(sock);
            delayconnect(debounceconnect, peer);
            return Nothing; } }

    /* Started async connect() -> wait for result. */
    {   iosubscription connectsub(sub, fd_t(sock).poll(POLLOUT));
        auto deadline(timestamp::now() + pool.cfg.connecttimeout);
        while (true) {
            if (deadline.inpast()) {
                ::close(sock);
                /* No debounce this time: the connect timeout achieves the
                 * same thing. */
                return Nothing; }
            auto _deadline(checktimeouts(calls, cl, idledat, false, NULL));
            if (_deadline == Nothing || _deadline.just().after(deadline)) {
                _deadline = deadline; }
            auto ss(sub.wait(io, _deadline));
            if (finished(calls, cl)) {
                ::close(sock);
                return Nothing; }
            /* Other subscriptions handled by checktimeouts() */
            if (ss != &connectsub) continue;
            auto pfd(fd_t(sock).poll(POLLOUT));
            if (::poll(&pfd, 1, 0) < 0) {
#ifndef COVERAGESKIP
                auto err(error::from_errno());
                err.warn("connect poll() " + fields::mk(agent));
                harderror(calls, err, cl);
                return Nothing;
#endif
            }
            if (pfd.revents & POLLOUT) {
                auto err(socketerr(sock, peer));
                if (err.isfailure()) {
                    err.failure().warn(
                        "connecting to " + fields::mk(peer) +
                        " for " + fields::mk(agent));
                    ::close(sock);
                    if (err != error::timeout) {
                        delayconnect(debounceconnect, peer); }
                    return Nothing; }
                else break; } } }

    /* We have a connection.  Send the HELLO request. */
    auto helloseq(nextseqnr);
    nextseqnr++;
    {   buffer txbuf;
        serialise1 s(txbuf);
        proto::reqheader(-1,
                         version::current,
                         interfacetype::meta,
                         helloseq)
            .serialise(s);
        proto::meta::tag::hello.serialise(s);
        assert(txbuf.avail() <= proto::maxmsgsize);
        *txbuf.linearise<unsigned>(txbuf.offset()) = (unsigned)txbuf.avail();
        iosubscription out(sub, fd_t(sock).poll(POLLOUT));
        auto deadline(timestamp::now() + pool.cfg.hellotimeout);
        while (true) {
            auto res(txbuf.sendfast(fd_t(sock)));
            if (res.isfailure() && res != error::wouldblock) {
                ::close(sock);
                return Nothing; }
            if (txbuf.empty()) break;
            if (deadline.inpast()) {
                logmsg(loglevel::info,
                       "timeout sending hello to " + fields::mk(agent));
                ::close(sock);
                return Nothing; }
            auto _deadline(checktimeouts(calls, cl, idledat, false, NULL));
            if (_deadline == Nothing || _deadline.just().after(deadline)) {
                _deadline = deadline; }
            auto ss(sub.wait(io, _deadline));
            if (finished(calls, cl)) {
                ::close(sock);
                return Nothing; }
            if (ss == &out) out.rearm(); } }

    /* Wait for and process the HELLO response. */
    iosubscription in(sub, fd_t(sock).poll(POLLIN));
    /* Use a local rxbuffer, rather than the main one used for other
     * responses, because the peer shouldn't be sending us any
     * messages other than the HELLO response until after we've sent
     * it an actual command. */
    buffer rxbuffer;
    while (true) {
        deserialise1 ds(rxbuffer);
        proto::respheader hdr(ds);
        version minv(ds);
        version maxv(ds);
        list<interfacetype> types(ds);
        if (ds.status() == error::underflowed) {
            auto err(rxbuffer.receivefast(fd_t(sock)));
            if (err == error::wouldblock) {
                auto ss(
                    sub.wait(
                        io, checktimeouts(calls, cl, idledat, false, NULL)));
                if (finished(calls, cl)) {
                    ::close(sock);
                    return Nothing; }
                if (ss == &in) in.rearm(); }
            else if (err.isfailure()) {
                ::close(sock);
                return Nothing; }
            continue; }
        if (hdr.status.isfailure()) ds.fail(hdr.status.failure());
        else if (hdr.size > proto::maxmsgsize) ds.fail(error::overflowed);
        else if (hdr.seq != helloseq) ds.fail(error::invalidmessage);
        else if (hdr.size != rxbuffer.avail()) ds.fail(error::invalidmessage);
        else if (types.empty()) ds.fail(error::invalidmessage);
        else if (minv > version::current || maxv < version::current) {
            ds.fail(error::badversion); }
        if (ds.isfailure()) {
            /* Getting an error back from HELLO is a hard failure. */
            harderror(calls, ds.failure(), cl);
            ::close(sock);
            return Nothing; }
        /* XXX we discard the server's advertised interface list.  Is
         * there much point in having it? */
        /* Connected successfully -> reset debouncer. */
        debounceconnect = Nothing;
        /* HELLO completed successfully -> ready to move to main
         * phase. */
        return fd_t(sock); } }

void
CONN::workphase(clientio io,
                list<nnp<CALL> > &calls,
                subscriber &sub,
                fd_t fd,
                const subscription &shutdownsub,
                const subscription &newcallssub,
                proto::sequencenr &nextseqnr,
                maybe<timestamp> &idledat,
                connlock cl) {
    /* Connection established and we've negotiated the protocol
     * version.  Deal with messages. */

    buffer txbuffer;
    /* Need to retransmit everything in the backlog. */
    queuetx(calls, txbuffer, nextseqnr, cl);

    /* Stays connected all the time, even when we're not expecting
     * anything back, to pick up errors. */
    iosubscription insub(sub, fd.poll(POLLIN));
    /* Only armed when we have stuff to send. */
    iosubscription outsub(sub, fd.poll(POLLOUT));
    bool outarmed(true);

    buffer rxbuffer;
    while (true) {
        if (!txbuffer.empty() && !outarmed) {
            outsub.rearm();
            outarmed = true; }
        auto ss(sub.wait(
                    io, checktimeouts(calls, cl, idledat, true, &txbuffer)));
        if (ss == NULL || ss == &shutdownsub) {
            if (finished(calls, cl)) return; }
        else if (ss == &newcallssub) {
            /* Suck the newcalls list into the calls list, serialising
             * them into the TX buffer as we do so.  The abort list
             * will be handled by checktimeouts(). */
            list<nnp<CALL> > nn;
            mux.locked([this, &nn] (mutex_t::token tok) {
                    nn.transfer(newcalls(tok)); });
            queuetx(nn, txbuffer, nextseqnr, cl);
            calls.transfer(nn);
            /* Do a quick send, while we're here, to avoid a trip
             * through the iosubscription thread.  Errors will leave
             * stuff in the buffer and will eventually be handled by the slow
             * path. */
            if (!txbuffer.empty()) (void)txbuffer.sendfast(fd); }
        else if (ss == &outsub) {
            assert(outarmed);
            outarmed = false;
            if (txbuffer.empty()) continue;
            auto err(txbuffer.sendfast(fd));
            if (err == error::wouldblock) continue;
            if (err.isfailure()) {
                /* Treat any failure here as a lost connection and
                 * restart the connect machine. */
                err.failure().warn("transmitting to " + fields::mk(agent));
                /* Might as well check if there are any viable
                 * responses waiting for us before we give up. */
                (void)rxbuffer.receivefast(fd);
                while (processresponse(rxbuffer, calls, cl).issuccess()) { }
                return; } }
        else if (ss == &insub) {
            auto err(rxbuffer.receivefast(fd));
            if (err == error::wouldblock) continue;
            if (err.isfailure()) {
                err.failure().warn("receiving from " + fields::mk(agent));
                return; }
            insub.rearm();
            while (true) {
                auto r(processresponse(rxbuffer, calls, cl));
                if (r == error::underflowed) break;
                else if (r.isfailure()) return; } }
        else abort(); } }

void
CONN::delayconnect(maybe<pair<peername, timestamp> > &debounceconnect,
                   const peername &peer) {
    debounceconnect =
        mkpair(peer, timestamp::now() + pool.cfg.debounceconnect); }

nnp<CALL>
CONN::call(maybe<timestamp> deadline,
           interfacetype type,
           const std::function<connpool::serialiser> &s,
           const deserialiser &ds,
           mutex_t::token token) {
    assert(!dying(token));
    /* XXX there should be a fast path here which sends stuff directly
     * without waiting for the connection thread (if we already have a
     * connection) */
    auto res(_nnp(*new CALL(*this, deadline, type, s, ds)));
    newcalls(token).pushtail(res);
    callschanged.publish();
    return res; }

CONN::conn(const constoken &t,
           const agentname &_agent,
           POOL &_pool,
           maybe<mutex_t::token> *token)
    : thread(t),
      pool(_pool),
      mux(),
      deathsub(_pool.sub, thread::pub(), this),
      agent(_agent),
      _dying(false),
      _newcalls(),
      _aborted(),
      callschanged() {
    *token = mux.lock(); }

void
CONN::run(clientio io) {
    subscriber sub;
    subscription shutdownsub(sub, pool.shutdown.pub());
    subscription newcallssub(sub, callschanged);

    /* The connlock isn't really a lock, it's just a tag to indicate
     * that a particular operation will hold up the connection
     * thread. */
    connlock cl;

    maybe<pair<peername, timestamp> > debounceconnect(Nothing);
    list<nnp<CALL> > calls;
    maybe<timestamp> idledat(timestamp::now());
    /* Something (a) recognisable and (b) large enough to flush out 32
     * bit truncation bugs. */
    proto::sequencenr nextseqnr(0x156782345ul);

    shutdownsub.set();
    newcallssub.set();

    while (!finished(calls, cl)) {
        auto fd(connectphase(io,
                             sub,
                             debounceconnect,
                             calls,
                             cl,
                             idledat,
                             nextseqnr));
        if (fd.isjust()) {
            workphase(io,
                      calls,
                      sub,
                      fd.just(),
                      shutdownsub,
                      newcallssub,
                      nextseqnr,
                      idledat,
                      cl);
            fd.just().close();
            fd = Nothing; } }
    assert(calls.empty());
    assert(_dying);
    assert(_newcalls.empty());
    assert(_aborted.empty()); }

/* --------------------------- Call implementation ------------------------ */
CALL::impl(CONN &_conn,
           maybe<timestamp> _deadline,
           interfacetype _type,
           const std::function<connpool::serialiser> &_serialiser,
           const deserialiser &_deserialise)
    : api(),
      conn(_conn),
      deadline(_deadline),
      type(_type),
      serialiser(_serialiser),
      deserialise(_deserialise),
      pub(),
      mux(),
      _res(Nothing),
      _aborted(false),
      _refcount(1),
      _seqnr(Nothing) {}

maybe<connpool::asynccall::token>
CALL::finished() const {
    if (mux.locked<bool>([this] (mutex_t::token tok) {
                return res(tok) != Nothing; })) {
        return token(); }
    else return Nothing; }

connpool::asynccall::token
CALL::finished(clientio io) const {
    auto t(finished());
    if (t == Nothing) {
        subscriber sub;
        subscription ss(sub, pub);
        t = finished();
        while (t == Nothing) {
            sub.wait(io);
            t = finished(); } }
    return t.just(); }

orerror<void>
CALL::pop(token t) {
    auto r(res(t).just());
    dereference();
    return r; }

orerror<void>
CALL::abort() {
    if (mux.locked<bool>([this] (mutex_t::token tok) {
                if (res(tok) != Nothing) return true;
                aborted(tok) = true;
                return false; })) {
        /* Call finished before we aborted it.  No need to put it in
         * the abort queue. */ }
    else {
        conn.mux.locked([this] (mutex_t::token tok) {
                reference();
                conn.aborted(tok).pushtail(_nnp(*this));
                conn.callschanged.publish(); }); }
    /* Setting aborted and notifying the conn's callschanged publisher
     * is guaranteed to complete the call quickly, so we don't need a
     * full clientio token here. */
    return pop(finished(clientio::CLIENTIO)); }
