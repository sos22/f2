#include "eqserver.H"

#include "buffer.H"
#include "fields.H"
#include "logging.H"
#include "util.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "thread.tmpl"

#include "fieldfinal.H"

#define EVENT geneventqueue::event
#define QUEUE geneventqueue::impl
#define SUBSCRIPTION QUEUE::sub
#define SERVER eqserver::impl

/* Lock ordering:
 *
 * -- server and queue locks are *unordered*.  If you have to acquire
 *    both, acquire the detach lock before acquiring either.
 * -- The queue lock is ordered before the rpcservice txlock.
 */

/* Deadlock mediation lock.  The queue and server locks don't have a
 * well-defined order, so any time you acquire both of them you need
 * to acquire this one first. */
/* XXX not clear this actually buys us anything over just using this in place
 * of the server lock. */
static mutex_t
detachlock;

class EVENT {
    /* An ID for the event.  This is only assigned once we've finished
     * doing the serialisation, so that we don't have to hold any
     * locks until we've finished serialising the content. */
    /* Note that the event used here isn't quite the same as the event
     * returned to subscribers.  In the natural implementation,
     * eventids would be comparable across subscriptions *almost* all
     * the time, so to avoid subtle bugs we deliberately add the
     * subscriber ID to this event ID whenever we report it to a
     * client. */
public: maybe<proto::eq::eventid> id;
public: buffer content;
public: event() : id(Nothing), content() {} };

class QUEUE {
public: class sub {
        /* Unique identifier for this subscription */
    public: proto::eq::subscriptionid id;
        /* The next event ID which has not been trimmed by this
         * subscription.  We can cheaply drop an event which has been
         * trimmed by every subscription. */
        /* (We can drop other events as well, at the cost of causing
         * much more work for clients.) */
    public: proto::eq::eventid next;
    public: explicit sub(proto::eq::eventid _next)
        : id(proto::eq::subscriptionid::invent()),
          next(_next) {} };

    /* An outstanding call to poll from a remote peer. */
public: class poll {
    public: nnp<rpcservice2::incompletecall> ic;
    public: proto::eq::subscriptionid subid;
    public: proto::eq::eventid eid;
    public: subscription abandonmentsub;
    public: poll(nnp<rpcservice2::incompletecall> ic,
                 proto::eq::subscriptionid subid,
                 subscriber &,
                 proto::eq::eventid,
                 QUEUE *q); };
public: geneventqueue api;
public: const eventqueueconfig config;

public: mutex_t mux;

    /* Event ID of the last event dropped.  We've dropped everything
     * before that as well. */
public: proto::eq::eventid _lastdropped;
public: proto::eq::eventid &lastdropped(mutex_t::token) { return _lastdropped; }

    /* Next event ID to allocate. */
public: proto::eq::eventid _nextid;
public: proto::eq::eventid &nextid(mutex_t::token) { return _nextid; }

    /* List of all of the events which have been generated and not yet
     * discarded. */
public: list<event> _events;
public: list<event> &events(mutex_t::token) { return _events; }

    /* List of all outstanding subscriptions. */
public: list<sub> _subscriptions;
public: list<sub> &subscriptions(mutex_t::token) { return _subscriptions; }

    /* All outstanding polls against this queue. */
public: list<poll> _polls;
public: list<poll> &polls(mutex_t::token) { return _polls; }

    /* What server is exposing this queue?  NULL if we've been
     * detached. */
public: SERVER *_server;
public: SERVER *&server(mutex_t::token) { return _server; }

public: impl(const proto::eq::genname &_name,
             const eventqueueconfig &_config)
    : api(_name),
      config(_config),
      mux(),
      _lastdropped(proto::eq::eventid(0)),
      _nextid(proto::eq::eventid(1000)),
      _subscriptions(),
      _polls(),
      _server(NULL) {}

    /* Drop events from the queue, if allowed to do so by the
     * subscriptions list. */
public: void trim(mutex_t::token); };

/* The only use of the server thread is to wait for polls to be
 * abandoned so that we can clean up. */
class SERVER : public thread {
public: eqserver api;

public: mutex_t mux;

public: explicit impl(const constoken &t);
public: void run(clientio);

public: list<nnp<QUEUE> > _queues;
public: list<nnp<QUEUE> > &queues(mutex_t::token) { return _queues; }

    /* The thread's main subscriber block.  Pollers all create
     * subscriptions from this to their abandonment publisher, using
     * the queue as the data cookie.  All other subscriptions have a
     * NULL cookie. */
public: subscriber sub;

public: waitbox<void> shutdown;

public: orerror<pair<nnp<QUEUE>, mutex_t::token> >getlockedqueue(
    proto::eq::genname n);
public: orerror<void> subscribe(clientio,
                                deserialise1 &,
                                nnp<rpcservice2::incompletecall>,
                                rpcservice2::onconnectionthread);
public: orerror<void> get(clientio,
                          deserialise1 &,
                          nnp<rpcservice2::incompletecall>,
                          rpcservice2::onconnectionthread);
public: orerror<void> wait(clientio,
                           deserialise1 &,
                           nnp<rpcservice2::incompletecall>,
                           rpcservice2::onconnectionthread);
public: orerror<void> trim(clientio,
                           deserialise1 &,
                           nnp<rpcservice2::incompletecall>,
                           rpcservice2::onconnectionthread);
public: orerror<void> unsubscribe(clientio,
                                  deserialise1 &,
                                  nnp<rpcservice2::incompletecall>,
                                  rpcservice2::onconnectionthread); };

eventqueueconfig::eventqueueconfig() : queuelimit(50) {}

eventqueueconfig
eventqueueconfig::dflt() { return eventqueueconfig(); }

/* --------------------------- geneventqueue API -------------------------- */
geneventqueue::impl &
geneventqueue::implementation() { return *containerof(this, impl, api); }

geneventqueue::geneventqueue(const proto::eq::genname &_name)
    : name(_name) {}

nnp<geneventqueue>
geneventqueue::build(const proto::eq::genname &_name,
                     const eventqueueconfig &_config) {
    return _nnp((new QUEUE(_name, _config))->api); }

geneventqueue::queuectxt::queuectxt(geneventqueue &q)
    : inner(NULL),
      serialiser(Nothing) {
    /* Drop anything which arrives when we have no subscribers. */
    auto &qi(q.implementation());
    if (!qi.mux.locked<bool>([this, &qi] (mutex_t::token tok) {
                return qi.subscriptions(tok).empty(); })) {
        inner = list<event>::mkpartial();
        serialiser.mkjust(inner->val().content); }
    else logmsg(loglevel::verbose, "drop queue message with no subscribers"); }

void
geneventqueue::queuectxt::finish(geneventqueue &q,
                                 rpcservice2::acquirestxlock atl) {
    geneventqueue::finishingpush();
    assert(inner != NULL);
    auto &qi(q.implementation());
    auto token(qi.mux.lock());
    if (qi.subscriptions(token).empty()) {
        /* No point queueing anything which nobody's going to
         * receive. */
        logmsg(loglevel::verbose, "drop queue message; subscribers lost");
        delete inner;
        qi.mux.unlock(&token);
        return; }
    
    /* Add it to the queue. */
    inner->val().id = qi.nextid(token);
    qi.nextid(token)++;
    auto &e(qi.events(token).pushtail(*inner));
    if (qi.events(token).length() > qi.config.queuelimit) {
        logmsg(loglevel::verbose,
               "drop event: queue " + q.name.field() + " overflowed");
        assert(qi.lastdropped(token) < qi.events(token).peekhead().id.just());
        qi.lastdropped(token) = qi.events(token).peekhead().id.just();
        qi.events(token).drophead();
        assert(qi.events(token).length() <= qi.config.queuelimit); }
    else {
        logmsg(loglevel::debug,
               "queue event " + inner->val().id.field() +
               " on " + q.name.field() + " with no drop; " +
               fields::mk(qi.events(token).length()) + " of " +
               fields::mk(qi.config.queuelimit) + " outstanding"); }
    /* Wake up everyone who's polling for events. */
    while (!qi.polls(token).empty()) {
        auto &p(qi.polls(token).peekhead());
        auto ic(p.ic);
        logmsg(loglevel::debug,
               "wake poller for " + p.eid.field() +
               " on " + p.subid.field());
        /* Kill abandonment sub before completing the call. */
        qi.polls(token).drophead();
        ic->complete(
            [e] (serialise1 &s, mutex_t::token) {  s.push(e.content); },
            atl); }
    qi.mux.unlock(&token); }

void
geneventqueue::destroy(rpcservice2::acquirestxlock atl) {
    auto &i(implementation());
    auto toptoken(detachlock.lock());
    auto qtoken(i.mux.lock());
    if (i.server(qtoken) != NULL) {
        auto stoken(i.server(qtoken)->mux.lock());
        /* Acquired all the locks we need -> no longer need the
         * deadlock avoidance lock. */
        detachlock.unlock(&toptoken);
        /* Hold server lock -> remove ourselves from the queue
         * list. */
        for (auto it(i.server(qtoken)->queues(stoken).start());
             true;
             it.next()) {
            if (*it == &i) {
                it.remove();
                break; } }
        /* Server doesn't know about us any more -> can drop server
         * lock. */
        i.server(qtoken)->mux.unlock(&stoken);
        i.server(qtoken) = NULL; }
    else {
        /* Already detached -> don't need to touch the server. */
        detachlock.unlock(&toptoken); }
    /* Privatised structure -> no longer need lock. */
    i.mux.unlock(&qtoken);
    /* Pending polls will now never complete. */
    while (!i._polls.empty()) {
        auto ic(i._polls.peekhead().ic);
        i._polls.drophead();
        ic->fail(error::badqueue, atl); }
    /* Drop all outstanding events. */
    i._events.flush();
    logmsg(loglevel::verbose,
           "destroy " + fields::mk((unsigned long)this).base(16));
    delete &i; }

geneventqueue::~geneventqueue() {}

tests::hookpoint<void>
geneventqueue::finishingpush([] {});

/* ---------------------- geneventqueue implementation --------------------- */
QUEUE::poll::poll(nnp<rpcservice2::incompletecall> _ic,
                  proto::eq::subscriptionid _subid,
                  subscriber &subscribe,
                  proto::eq::eventid _eid,
                  QUEUE *q)
    : ic(_ic),
      subid(_subid),
      eid(_eid),
      abandonmentsub(subscribe, _ic->abandoned().pub, q) {}

void
QUEUE::trim(mutex_t::token tok) {
    if (subscriptions(tok).empty()) {
        /* Lost the last subscriber -> no need to keep any events
         * around at all. */
        events(tok).flush();
        lastdropped(tok) = nextid(tok);
        /* Leave a gap in the sequence number space, to flush out bad
         * caching bugs on the client. */
        nextid(tok) += 10000;
        return; }
    maybe<proto::eq::eventid> trimto(Nothing);
    for (auto it(subscriptions(tok).start()); !it.finished(); it.next()) {
        if (trimto == Nothing || trimto.just() > it->next) trimto = it->next; }
    /* trimto can never move backwards. */
    assert(trimto.just() >= lastdropped(tok));
    if (trimto == lastdropped(tok)) return;
    /* Apply the trim. */
    for (auto it(events(tok).start());
         !it.finished() && it->id.just() <= trimto.just();
         it.remove()) { }
    lastdropped(tok) = trimto.just(); }

/* ------------------------------- eqserver API ---------------------------- */
SERVER &
eqserver::implementation() { return *containerof(this, impl, api); }

void
eqserver::registerqueue(geneventqueue &what) {
    auto &i(implementation());
    /* No lock on the queue because this is only called while it's
     * still thread-private. */
    assert(what.implementation()._server == NULL);
    what.implementation()._server = &i;
    i.mux.locked([this, &i, &what] (mutex_t::token tok) {
            i.queues(tok).pushtail(what.implementation()); }); }

nnp<eqserver>
eqserver::build() {
    return _nnp( thread::start<SERVER>(fields::mk("eqserver"))->api); }

orerror<void>
eqserver::called(clientio io,
                 deserialise1 &ds,
                 nnp<rpcservice2::incompletecall> ic,
                 rpcservice2::onconnectionthread oct) {
    auto &i(implementation());
    using namespace proto::eq;
    tag t(ds);
    if (t == tag::subscribe) return i.subscribe(io, ds, ic, oct);
    else if (t == tag::get) return i.get(io, ds, ic, oct);
    else if (t == tag::wait) return i.wait(io, ds, ic, oct);
    else if (t == tag::trim) return i.trim(io, ds, ic, oct);
    else if (t == tag::unsubscribe) return i.unsubscribe(io, ds, ic, oct);
    else return error::unrecognisedmessage; }

void
eqserver::destroy() {
    auto &i(implementation());
    i.shutdown.set();
    /* Quick because shutdown is set. */
    i.join(clientio::CLIENTIO); }

eqserver::~eqserver() { assert(implementation()._queues.empty()); }

/* -------------------------- eqserver implementation ----------------------- */

SERVER::impl(const constoken &t)
    : thread(t),
      api(),
      mux(),
      _queues(),
      sub() {}

void
SERVER::run(clientio io) {
    subscription shutdownsub(sub, shutdown.pub);
    rpcservice2::acquirestxlock atl(io);
    while (!shutdown.ready()) {
        auto notified(sub.wait(io));
        if (notified == &shutdownsub) continue;
        assert(notified->data != NULL);
        auto q((QUEUE *)notified->data);
        auto dt(detachlock.lock());
        auto st(mux.lock());
        /* The queue might have gone away while we were extracting the
         * subscription from the subscriber, so we need to pull it
         * back out of the queue list, rather than just dereferencing
         * it straight off. */
        for (auto it(queues(st).start()); !it.finished(); it.next()) {
            if (&**it != q) continue;
            /* It's still in the list -> we can safely dereference
             * it. */
            auto qt(q->mux.lock());
            mux.unlock(&st);
            detachlock.unlock(&dt);
            /* Check all of the polls and complete any which have been
             * abandoned. */
            bool remove;
            list<nnp<rpcservice2::incompletecall> > abandoned;
            for (auto it2(q->polls(qt).start());
                 !it2.finished();
                 remove ? it2.remove()
                        : it2.next()) {
                if (it2->ic->abandoned().ready()) {
                    abandoned.pushtail(it2->ic);
                    remove = true; }
                else remove = false; }
            q->mux.unlock(&qt);
            for (auto it2(abandoned.start()); !it2.finished(); it2.remove()) {
                /* The it.remove() above will have already removed the
                 * abandonment subscription from the subscriber, so
                 * we're free to fail the call. */
                (*it2)->fail(error::aborted, atl); }
            q = NULL;
            break; }
        if (q != NULL) {
            /* Not a big thing, but probably worth a debug message. */
            logmsg(loglevel::debug,
                   "received poll abandonment notification on queue " +
                   fields::mk((unsigned long)q).base(16) +
                   " after it was shut down"); } }
    /* Detach all the remaining queues. */
    auto dt(detachlock.lock());
    auto tok(mux.lock());
    for (auto it(queues(tok).start()); !it.finished(); it.next()) {
        auto q(*it);
        q->mux.locked([this, q] (mutex_t::token qt) {
                assert(q->server(qt) == this);
                q->server(qt) = NULL; }); }
    queues(tok).flush();
    mux.unlock(&tok);
    detachlock.unlock(&dt); }


/* Lookup a queue, returning either it or an error.  On success, the
 * queue is returned with the queue lock held (but not the server or
 * detach locks). */
orerror<pair<nnp<QUEUE>, mutex_t::token> >
SERVER::getlockedqueue(proto::eq::genname n) {
    auto dt(detachlock.lock());
    auto servertok(mux.lock());
    for (auto it(queues(servertok).start()); !it.finished(); it.next()) {
        auto q(*it);
        if (q->api.name != n) continue;
        auto queuetok(q->mux.lock());
        mux.unlock(&servertok);
        detachlock.unlock(&dt);
        return mkpair(_nnp(*q), queuetok); }
    mux.unlock(&servertok);
    detachlock.unlock(&dt);
    return error::badqueue; }

orerror<void>
SERVER::subscribe(clientio io,
                  deserialise1 &ds,
                  nnp<rpcservice2::incompletecall> ic,
                  rpcservice2::onconnectionthread oct) {
    auto _q(getlockedqueue(proto::eq::genname(ds)));
    if (_q.isfailure()) return _q.failure();
    auto q(_q.success().first());
    auto token(_q.success().second());
    
    auto s(list<QUEUE::sub>::mkpartial(q->nextid(token)));
    auto sid(s->val().id);
    auto next(s->val().next);
    q->subscriptions(token).pushtail(s);
    q->mux.unlock(&token);
    ic->complete(
        [next, sid] (serialise1 &ser,
                     mutex_t::token /* txlock */,
                     rpcservice2::onconnectionthread /* oct */) {
            sid.serialise(ser);
            next.serialise(ser); },
        io,
        oct);
    return Success; }

orerror<void>
SERVER::get(clientio io,
            deserialise1 &ds,
            nnp<rpcservice2::incompletecall> ic,
            rpcservice2::onconnectionthread oct) {
    auto _q(getlockedqueue(proto::eq::genname(ds)));
    proto::eq::subscriptionid subid(ds);
    proto::eq::eventid eid(ds);
    if (_q.isfailure()) return _q.failure();
    auto q(_q.success().first());
    auto token(_q.success().second());
    
    if (q->lastdropped(token) >= eid) {
        q->mux.unlock(&token);
        return error::eventsdropped; }
    for (auto it(q->subscriptions(token).start());
         !it.finished();
         it.next()) {
        if (it->id != subid) continue;
        if (eid < it->next) {
            /* In principle, we don't actually need to do this check,
             * because the clietn will never ask for something which
             * it's trimmed.  Doesn't hurt to have a debug check,
             * though. */
            logmsg(loglevel::debug,
                   "attempt to fetch event " + eid.field() +
                   " on queue " + q->api.name.field() +
                   " sub " + subid.field() +
                   ", but that's already trimmed to " +
                   it->next.field());
            q->mux.unlock(&token);
            return error::invalidparameter; }
        if (q->nextid(token) <= eid) {
            logmsg(loglevel::verbose,
                   "get " + eid.field() + " which is not ready "
                   "(have to " + q->nextid(token).field() + "-1)");
            q->mux.unlock(&token);
            ic->complete(
                [] (serialise1 &s,
                    mutex_t::token /* txlock */,
                    rpcservice2::onconnectionthread /* oct */) {
                    maybe<buffer>(Nothing).serialise(s); },
                io,
                oct); }
        else {
            logmsg(loglevel::verbose,
                   "get " + eid.field() + " which is ready");
            maybe<nnp<buffer> > buf(Nothing);
            /* Must eventually hit the desired thing, because it's in
             * range. */
            for (auto it2(q->events(token).start());
                 buf == Nothing;
                 it2.next()) {
                if (it2->id == eid) buf.mkjust(it2->content); }
            /* Note that we're acquiring the service TX lock while
             * holding the queue lock, which is a little bit
             * skanky. */
            ic->complete(
                [&buf] (serialise1 &s,
                        mutex_t::token /* txlock */,
                        rpcservice2::onconnectionthread /* oct */) {
                    buf.serialise(s); },
                rpcservice2::acquirestxlock(io),
                oct);
            q->mux.unlock(&token); }
        return Success; }
    q->mux.unlock(&token);
    return error::badsubscription; }

orerror<void>
eqserver::impl::wait(
    clientio io,
    deserialise1 &ds,
    nnp<rpcservice2::incompletecall> ic,
    rpcservice2::onconnectionthread oct) {
    auto _q(getlockedqueue(proto::eq::genname(ds)));
    proto::eq::subscriptionid subid(ds);
    proto::eq::eventid eid(ds);
    if (_q.isfailure()) return _q.failure();
    auto q(_q.success().first());
    auto token(_q.success().second());
    
    if (q->nextid(token) > eid) {
        /* It's already ready. */
        q->mux.unlock(&token);
        logmsg(loglevel::debug,
               "wait for " + eid.field() + " on " + subid.field() +
               "; already ready");
        ic->complete(Success, rpcservice2::acquirestxlock(io), oct); }
    else {
        /* queue up a poller. */
        logmsg(loglevel::debug,
               "wait for " + eid.field() + " on " + subid.field() +
               "; not ready (" + q->nextid(token).field() + " next)");
        q->polls(token).append(ic, subid, sub, eid, q);
        q->mux.unlock(&token); }
    return Success; }

orerror<void>
eqserver::impl::trim(
    clientio io,
    deserialise1 &ds,
    nnp<rpcservice2::incompletecall> ic,
    rpcservice2::onconnectionthread oct) {
    auto _q(getlockedqueue(proto::eq::genname(ds)));
    proto::eq::subscriptionid subid(ds);
    proto::eq::eventid eid(ds);
    if (_q.isfailure()) return _q.failure();
    auto q(_q.success().first());
    auto token(_q.success().second());
    
    if (q->lastdropped(token) >= eid) {
        /* Already done. */
        q->mux.unlock(&token);
        ic->complete(Success, io, oct);
        return Success; }
    if (q->nextid(token) <= eid) {
        /* Can't trim past the end of the queue */
        logmsg(loglevel::verbose,
               "trim to " + eid.field() + ", but only have to " +
               q->nextid(token).field());
        q->mux.unlock(&token);
        return error::toosoon; }
    maybe<nnp<QUEUE::sub> > subs(Nothing);
    for (auto it(q->subscriptions(token).start());
         !it.finished() && subs == Nothing;
         it.next()) {
        if (it->id == subid) subs.mkjust(*it); }
    if (subs == Nothing) {
        /* Can't trim a subscription which we've already lost. */
        q->mux.unlock(&token);
        return error::badsubscription; }
    
    /* Ignore attempts to rewind the trim marker on a subscription,
     * beyond producing a warning. */
    if (subs.just()->next > eid) {
        logmsg(loglevel::debug,
               "trimming " + q->api.name.field() + " sub " +
               subid.field() + " to " + eid.field() +
               ", but that was already trimmed to " +
               subs.just()->next.field());
        q->mux.unlock(&token);
        ic->complete(Success, io, oct);
        return Success; }
    
    /* Clients aren't really allowed to trim something which they're
     * polling, but it can sometimes look like they have due to
     * failures and reordered retransmits.  Spit out a warning. */
    for (auto it(q->polls(token).start());
         !it.finished();
         it.next()) {
        if (it->subid == subid && it->eid <= eid) {
            logmsg(loglevel::debug,
                   "trimming " + eid.field() + " on " +
                   q->api.name.field() + " subscription " +
                   subid.field() + ", but that's still being polled"); } }
    
    /* Advance the subscription trim point. */
    subs.just()->next = eid;
    
    /* And actually perform the trim. */
    q->trim(token);
    
    /* We're done. */
    q->mux.unlock(&token);
    ic->complete(Success, io, oct);
    return Success; }

orerror<void>
eqserver::impl::unsubscribe(
    clientio io,
    deserialise1 &ds,
    nnp<rpcservice2::incompletecall> ic,
    rpcservice2::onconnectionthread oct) {
    auto _q(getlockedqueue(proto::eq::genname(ds)));
    proto::eq::subscriptionid subid(ds);
    if (_q.isfailure()) return _q.failure();
    auto q(_q.success().first());
    auto token(_q.success().second());
    
    for (auto it(q->subscriptions(token).start());
         !it.finished();
         it.next()) {
        if (it->id != subid) continue;
        it.remove();
        q->mux.unlock(&token);
        ic->complete(Success, io, oct);
        return Success; }
    q->mux.unlock(&token);
    return error::badqueue; }
