#include "rpcservice2.H"

#include <sys/socket.h>
#include <unistd.h>

#include "buffer.H"
#include "fields.H"
#include "logging.H"
#include "proto2.H"
#include "serialise.H"
#include "test.H"
#include "thread.H"
#include "timedelta.H"
#include "util.H"

#include "either.tmpl"
#include "fields.tmpl"
#include "list.tmpl"
#include "mutex.tmpl"
#include "orerror.tmpl"
#include "test.tmpl"
#include "thread.tmpl"
#include "waitbox.tmpl"

class rpcservice2::rootthread final : public thread {
public: rpcservice2 &owner;
public: listenfd const fd;
public: waitbox<void> shutdown;
public: waitbox<orerror<void> > initialisedone;
public: list<interfacetype> type; /* const once initialised */
public: beaconserver *beacon;
    /* The pause machine. All fields are protected by the pause
     * lock. We only do paused true->false edges if pausecount is
     * zero. */
public: mutex_t pauselock; /* Leaf lock */
    /* How many threads want us paused? */
public: unsigned _pausecount;
public: unsigned &pausecount(mutex_t::token) { return _pausecount; }
    /* Are we currently paused? */
public: bool _paused;
public: bool &paused(mutex_t::token) { return _paused; }
    /* Notified if pausecount goes 0->+ve or paused goes
     * false->true. */
public: publisher pausepub;
public: rootthread(const constoken &token,
                   rpcservice2 &_owner,
                   listenfd _fd,
                   const list<interfacetype> &_type)
    : thread(token),
      owner(_owner),
      fd(_fd),
      shutdown(),
      initialisedone(),
      type(_type),
      beacon(NULL),
      pauselock(),
      _pausecount(0),
      _paused(false),
      pausepub() {
    assert(!type.contains(interfacetype::meta));
    type.pushtail(interfacetype::meta); }
public: void run(clientio) final;
public: rpcservice2::pausetoken pause(clientio);
public: void unpause(rpcservice2::pausetoken); };

class rpcservice2::connworker final : public thread {
    /* Acquired from the const quotaavail() method */
public: mutable mutex_t _txlock;
public: mutex_t &txlock(acquirestxlock) const { return _txlock; }
public: buffer _txbuffer;
public: buffer &txbuffer(mutex_t::token) { return _txbuffer; }
public: const buffer &txbuffer(mutex_t::token) const { return _txbuffer; }
    /* XXX should maybe cache the length of this somewhere?  We need it
     * rather a lot. */
public: list<nnp<incompletecall> > _outstandingcalls;
public: list<nnp<incompletecall> > &outstandingcalls(mutex_t::token) {
        return _outstandingcalls; }
public: const list<nnp<incompletecall> > &
            outstandingcalls(mutex_t::token) const {
        return _outstandingcalls; }
public: rootthread &owner;
public: socket_t const fd;
public: publisher completedcall;
     /* Leaf lock, nests inside tx lock, acquired from const
      * quotavail() */
public: mutable mutex_t pauselock;
public: bool _wantpause;
public: bool &wantpause(mutex_t::token) { return _wantpause; }
public: bool _paused;
public: bool &paused(mutex_t::token) { return _paused; }
public: publisher pausepub;
public: connworker(
    const constoken &token,
    rootthread &_owner,
    socket_t _fd)
    : thread(token),
      _txlock(),
      _txbuffer(),
      _outstandingcalls(),
      owner(_owner),
      fd(_fd),
      completedcall(),
      pauselock(),
      _wantpause(false),
      _paused(false),
      pausepub() {}

public: void run(clientio) final;

public: void complete(
    orerror<void>,
    const std::function<void (serialise1 &, mutex_t::token)> &,
    proto::sequencenr,
    nnp<incompletecall>,
    acquirestxlock);
public: void complete(
    orerror<void>,
    const std::function<void (serialise1 &,
                              mutex_t::token,
                              onconnectionthread)> &doit,
    proto::sequencenr,
    nnp<incompletecall>,
    acquirestxlock,
    onconnectionthread oct);
public: orerror<void> processmessage(
    clientio io,
    const orerror<peername> &peer,
    const proto::reqheader &hdr,
    bool &donehello,
    deserialise1 &ds,
    onconnectionthread oct);
public: void calledhello(proto::sequencenr snr,
                         acquirestxlock atl,
                         onconnectionthread oct);
    /* Check whether we have tx buffer and outstanding call quota left
     * to accept another call from the peer. */
public: bool quotaavail(acquirestxlock) const;
public: bool quotaavail(mutex_t::token) const; };

rpcservice2config::rpcservice2config(const beaconserverconfig &_beacon,
                                     unsigned _maxoutstandingcalls,
                                     unsigned _txbufferlimit)
    : beacon(_beacon),
      maxoutstandingcalls(_maxoutstandingcalls),
      txbufferlimit(_txbufferlimit) {}

rpcservice2config
rpcservice2config::dflt(const clustername &cn,
                        const agentname &sn) {
    return rpcservice2config(
        beaconserverconfig::dflt(cn, sn),
        /* maxoutstandingcalls */
        64,
        /* bufferlimit */
        512 << 10); }

orerror<listenfd>
rpcservice2::open(const peername &pn) {
    auto s(::socket(pn.sockaddr()->sa_family,
                    SOCK_STREAM | SOCK_NONBLOCK,
                    0));
    if (s < 0) return error::from_errno();
    if (::bind(s, pn.sockaddr(), pn.sockaddrsize()) < 0 ||
        ::listen(s, 10) < 0) {
        ::close(s);
        return error::from_errno(); }
    else return listenfd(s); }

orerror<void>
rpcservice2::_initialise(clientio io) {
    auto beacon(beaconserver::build(config.beacon,
                                    root->type,
                                    port()));
    if (beacon.isfailure()) {
        root->initialisedone.set(beacon.failure());
        logmsg(loglevel::error,
               "cannot start rpcservice: beacon said " +
               beacon.failure().field());
        destroy(io);
        return beacon.failure(); }
    auto res(initialise(io));
    if (res.isfailure()) {
        root->initialisedone.set(res);
        beacon.success()->destroy(io);
        logmsg(loglevel::error,
               "cannot start rpcservice: initialise said " + res.field());
        destroy(io);
        return res; }
    root->beacon = beacon.success();
    root->initialisedone.set(res);
    logmsg(loglevel::debug, "initialised rpcservice");
    return res; }

orerror<void>
rpcservice2::initialise(clientio) { return Success; }

/* We start the root thread immediately, but won't actually let it do
 * anything until the derived class constructor returns. */
/* XXX this is exactly what the pausedthread structure was designed
 * for, but we can't use it here because of type issues.  Possibly
 * need to rethink API there. */
rpcservice2::rpcservice2(const constoken &ct, interfacetype type)
    : config(ct.config),
      root(thread::start<rootthread>(
               "R:" + fields::mk(ct.pn),
               *this,
               ct.fd,
               list<interfacetype>::mk(type))) { }
rpcservice2::rpcservice2(const constoken &ct, const list<interfacetype> &type)
    : config(ct.config),
      root(thread::start<rootthread>(
               "R:" + fields::mk(ct.pn),
               *this,
               ct.fd,
               type)) { }

peername::port
rpcservice2::port() const { return root->fd.localname().getport(); }

rpcservice2::onconnectionthread::onconnectionthread() {}

rpcservice2::incompletecall::incompletecall(
    connworker &_conn,
    proto::sequencenr _seqnr)
    : conn(_conn),
      seqnr(_seqnr) {}

void
rpcservice2::incompletecall::complete(
    const std::function<void (serialise1 &, mutex_t::token)> &doit,
    acquirestxlock atl) {
    conn.complete(Success, doit, seqnr, *this, atl); }

void
rpcservice2::incompletecall::fail(error e, acquirestxlock atl) {
    conn.complete(
        e,
        [] (serialise1 &, mutex_t::token) {},
        seqnr,
        *this,
        atl); }

void
rpcservice2::incompletecall::complete(
    const std::function<void (serialise1 &,
                              mutex_t::token,
                              onconnectionthread)> &doit,
    acquirestxlock atl,
    onconnectionthread oct) {
    conn.complete(Success, doit, seqnr, *this, atl, oct); }

void
rpcservice2::incompletecall::fail(
    error e,
    acquirestxlock atl,
    onconnectionthread oct) {
    conn.complete(e,
                  [] (serialise1 &, mutex_t::token, onconnectionthread) {},
                  seqnr,
                  *this,
                  atl,
                  oct); }

rpcservice2::incompletecall::~incompletecall() {}

rpcservice2::pausetoken
rpcservice2::pause(clientio io) { return root->pause(io); }

void
rpcservice2::unpause(pausetoken t) { root->unpause(t); }

void
rpcservice2::destroy(clientio io) {
    /* Starting a root shutdown will start shutdowns on all of the
     * workers, as well, which in turn starts shutdowns on all of the
     * outstanding incomplete calls. */
    root->shutdown.set();
    root->join(io);
    delete this; }

void
rpcservice2::destroying(clientio) {}

rpcservice2::~rpcservice2() {}

tests::hookpoint<void>
rpcservice2::clientdisconnected([] { } );

void
rpcservice2::rootthread::run(clientio io) {
    /* Wait for the initialise() method to finish.  Note that we don't
     * watch for shutdown here: initialise() must be finished before
     * anyone can get a pointer to the class to call destroy(), so
     * there wouldn't be much point. */
    if (initialisedone.get(io).isfailure()) {
        fd.close();
        if (beacon) beacon->destroy(io);
        return; }

    subscriber sub;
    subscription ss(sub, shutdown.pub());
    subscription ps(sub, pausepub);
    list<subscription> workers;
    iosubscription ios(sub, fd.poll());
    while (!shutdown.ready()) {
        assert(!_paused); /* No lock: this thread is the only one that
                           * changes it. */
        auto s(sub.wait(io));
        if (s == &ss) continue;
        else if (s == &ps) {
            if (!pauselock.locked<bool>([this] (mutex_t::token t) {
                        return pausecount(t) != 0; })) {
                continue; }
            /* Time to pause.  Tell all of our conn threads to
             * pause. */
            for (auto it(workers.start()); !it.finished(); it.next()) {
                auto w(static_cast<connworker *>(it->data));
                /* Note that it might already be paused, if the root
                 * goes pause->unpause->pause very quickly and the
                 * connection takes a long time to notice the unpause.
                 * That's fine. */
                w->pauselock.locked([w] (mutex_t::token t) {
                        assert(!w->wantpause(t));
                        w->wantpause(t) = true; });
                w->pausepub.publish(); }
            /* Wait for them to actually do it. */
            for (auto it(workers.start()); !it.finished(); it.next()) {
                auto w(static_cast<connworker *>(it->data));
                subscriber tmpsub;
                subscription tmpss(tmpsub, w->pausepub);
                while (!w->pauselock.locked<bool>([w] (mutex_t::token t) {
                            assert(w->wantpause(t));
                            return w->paused(t); })) {
                    tmpsub.wait(io); } }
            /* We are paused. */
            pauselock.locked([this] (mutex_t::token t) { paused(t) = true; });
            pausepub.publish();
            /* Wait to be told to unpause. */
            {   subscriber tmpsub;
                subscription tmpss(tmpsub, pausepub);
                auto t(pauselock.lock());
                while (pausecount(t) != 0) {
                    pauselock.unlock(&t);
                    tmpsub.wait(io);
                    t = pauselock.lock(); }
                paused(t) = false;
                pauselock.unlock(&t); }
            /* Unpause all connections. */
            for (auto it(workers.start()); !it.finished(); it.next()) {
                auto w(static_cast<connworker *>(it->data));
                w->pauselock.locked([w] (mutex_t::token t) {
                        assert(w->paused(t));
                        assert(w->wantpause(t));
                        w->wantpause(t) = false; });
                w->pausepub.publish(); }
            /* No wait for the connection unpause to actually
             * happen. */ }
        else if (s == &ios) {
            /* We have a new client. */
            auto newfd(fd.accept());
            if (newfd.isfailure()) {
                newfd.failure().warn(
                    "accepting on " + fields::mk(fd.localname()));
                /* Wait around for a bit before rearming so that a
                 * persistent failure doesn't completely spam the
                 * logs. */
                sub.wait(io, timestamp::now() + timedelta::seconds(1)); }
            ios.rearm();
            if (newfd.isfailure()) continue;
            auto r(thread::start<connworker>(
                       "S:" + fields::mk(fd.localname()),
                       *this,
                       newfd.success()));
            workers.append(sub, r->pub(), r)
                /* Avoid silliness if the client disconnects quickly. */
                .set(); }
        else {
            /* One of our worker threads died. */
            auto died(static_cast<connworker *>(s->data));
            auto death(died->hasdied());
            if (death == Nothing) continue;
            for (auto it(workers.start()); true; it.next()) {
                if (it->data == died) {
                    it.remove();
                    break; } };
            died->join(death.just());
            rpcservice2::clientdisconnected(); } }
    /* Time to die.  Wait for our client connections to drop.  Should
     * be quick because they watch our shutdown box. */
    while (!workers.empty()) {
        auto it(workers.start());
        auto w( (connworker *)it->data );
        it.remove();
        w->join(io); }
    if (beacon) beacon->destroy(io);
    fd.close();
    owner.destroying(io); }

rpcservice2::pausetoken
rpcservice2::rootthread::pause(clientio io) {
    auto t(pauselock.lock());
    pausecount(t)++;
    if (pausecount(t) == 1) pausepub.publish();
    if (!paused(t)) {
        subscriber sub;
        subscription ss(sub, pausepub);
        while (!paused(t)) {
            pauselock.unlock(&t);
            sub.wait(io);
            t = pauselock.lock();
        }
    }
    pauselock.unlock(&t);
    return rpcservice2::pausetoken(); }

void
rpcservice2::rootthread::unpause(pausetoken) {
    if (pauselock.locked<bool>([this] (mutex_t::token t) {
                assert(paused(t));
                assert(pausecount(t) > 0);
                pausecount(t)--;
                return pausecount(t) == 0; })) {
        pausepub.publish(); } }

void
rpcservice2::connworker::run(clientio io) {
    /* Only used for log messages. */
    auto peer(fd.peer());

    subscriber sub;
    /* We keep the in subscription armed unless we're trying to get
     * the other side to back off (either because we have too many
     * outstanding calls or because the TX buffer is too big). */
    iosubscription insub(sub, fd.poll(POLLIN));
    bool insubarmed = true;

    /* Out subscription is armed whenever we have stuff in the TX
     * buffer.  It starts off armed because that's the way the API
     * works. */
    iosubscription outsub(sub, fd.poll(POLLOUT));
    bool outsubarmed = true;

    /* Always watch for errors and EOF. */
    iosubscription errsub(sub, fd.poll(POLLERR));

    subscription ss(sub, owner.shutdown.pub());
    subscription completedsub(sub, completedcall);
    subscription pausesub(sub, pausepub);

    buffer rxbuffer;

    auto &config(owner.owner.config);

    /* Shouldn't have stuff to transmit yet, because we've not
     * processed any calls and can't have any responses. */
    assert(_txbuffer.empty());

    bool donehello = false;
    bool failed = false;

    acquirestxlock atl(io);

    /* Set if there's any point trying to deserialise out of the
     * rxbuffer without doing a further receive. */
    bool tryrecv = false;
    while (!failed && !owner.shutdown.ready()) {
        /* no lock, we're the only thread which changes it */
        assert(!_paused);
        /* The obvious races here are all handled by re-checking
         * everything when completedsub gets notified. */
        subscriptionbase *s;
        bool trysend = false;
        if (tryrecv) s = NULL;
        else {
            if (!insubarmed || !outsubarmed) {
                txlock(atl).locked([&] (mutex_t::token tok) {
                        if (!insubarmed && quotaavail(tok)) {
                            insub.rearm();
                            insubarmed = true; }
                        if (!outsubarmed && !txbuffer(tok).empty()) {
                            outsub.rearm();
                            outsubarmed = true; } }); }
            s = sub.wait(io); }

        /* Handled by loop condition */
        if (s == &ss) continue;
        if (s == &completedsub) {
            tryrecv = quotaavail(atl);
            continue; }
        if (s == &pausesub) {
            if (!pauselock.locked<bool>([this] (mutex_t::token t) {
                        return wantpause(t); })) {
                continue; }
            if (!txlock(atl).locked<bool>([this] (mutex_t::token t) {
                        return outstandingcalls(t).empty(); })) {
                continue; }
            auto t(pauselock.lock());
            assert(wantpause(t));
            paused(t) = true;
            pausepub.publish();
            subscriber tmpsub;
            subscription tmpps(tmpsub, pausepub);
            while (wantpause(t)) {
                pauselock.unlock(&t);
                tmpsub.wait(io);
                t = pauselock.lock(); }
            paused(t) = false;
            pauselock.unlock(&t);
            continue; }
        if (s == &insub || s == &errsub) {
            if (s == &insub) {
                assert(insubarmed);
                insubarmed = false; }
            else errsub.rearm();
            /* Use a quick RX check to pick up any errors and to
             * filter out spurious wake-ups. */
            {   auto res(rxbuffer.receivefast(fd));
                if (res == error::wouldblock) continue;
                if (res.isfailure()) {
                    /* Connection dead. */
                    failed = true;
                    continue; } }
            tryrecv = true; }
        if (tryrecv) {
            while (true) {
                if (!quotaavail(atl)) {
                    /* Calls might complete after we've dropped the
                     * lock, but that's okay because it'll notify the
                     * completed publisher and we'll pick it up next
                     * time around. */
                    tryrecv = false;
                    break; }

                deserialise1 ds(rxbuffer);
                proto::reqheader hdr(ds);
                if (ds.status() == error::underflowed) {
                    tryrecv = false;
                    break; }
                if (ds.isfailure()) {
                    ds.failure().warn("parsing message header from " +
                                      fields::mk(peer));
                    failed = true;
                    break; }
                if (hdr.size > proto::maxmsgsize) {
                    logmsg(loglevel::info,
                           "oversized message from " +
                           fields::mk(peer) + ": " +
                           fields::mk(hdr.size) + " > " +
                           fields::mk(proto::maxmsgsize));
                    failed = true;
                    break; }
                if (hdr.size > rxbuffer.avail()) {
                    /* Do a fast receive right now before going to
                     * sleep.  Ignore failures here; we'll pick them
                     * up later. */
                    (void)rxbuffer.receivefast(fd);
                    if (hdr.size > rxbuffer.avail()) {
                        tryrecv = false;
                        break; } }
                auto res(processmessage(
                             io,
                             peer,
                             hdr,
                             donehello,
                             ds,
                             onconnectionthread()));
                if (res.issuccess() &&
                    ds.offset() != rxbuffer.offset() + hdr.size) {
                    logmsg(loglevel::error,
                           "expected message to go from " +
                           fields::mk(rxbuffer.offset()) + " to " +
                           fields::mk(rxbuffer.offset() + hdr.size) +
                           "; actually went to " +
                           fields::mk(ds.offset()));
                    res = error::invalidparameter; }
                if (res.isfailure()) {
                    logmsg(loglevel::debug,
                           "error " +
                           fields::mk(res.failure()) +
                           " on connection to " +
                           fields::mk(peer));
                    failed = true;
                    break; }
                rxbuffer.discard(hdr.size); }
            trysend = true; }
        if (s == &outsub) {
            assert(outsubarmed);
            outsubarmed = false;
            trysend = true; }
        if (trysend) {
            auto res(txlock(atl).locked<orerror<void> >(
                         [this] (mutex_t::token tok) -> orerror<void> {
                             if (txbuffer(tok).empty()) return Success;
                             else return txbuffer(tok).sendfast(fd); }));
            if (res.isfailure() && res != error::wouldblock) failed = true;
            if (!tryrecv &&
                !rxbuffer.empty() &&
                quotaavail(atl)) {
                tryrecv = true; } } }
    {   auto token(txlock(atl).lock());

        /* Tell outstanding calls to abort. */
        for (auto it(outstandingcalls(token).start());
             !it.finished();
             it.next()) {
            auto i(*it);
            if (!i->abandoned(token).ready()) i->abandoned(token).set(); }

        /* Wait for them to finish. */
        auto laststatus(timestamp::now());
        subscriber smallsub;
        subscription c(smallsub, completedcall);
        while (!outstandingcalls(token).empty()) {
            txlock(atl).unlock(&token);
            auto sss = smallsub.wait(io, laststatus + timedelta::seconds(1));
            token = txlock(atl).lock();
            if (sss == NULL) {
                logmsg(loglevel::info,
                       "waiting to shut down service to " + fields::mk(peer) +
                       "; " + fields::mk(outstandingcalls(token).length()) +
                       " left");
                laststatus = timestamp::now(); } }
        txlock(atl).unlock(&token); }
    /* We're done */
    fd.close(); }

void
rpcservice2::connworker::complete(
    orerror<void> res,
    const std::function<void (serialise1 &, mutex_t::token)> &doit,
    proto::sequencenr seqnr,
    nnp<incompletecall> call,
    acquirestxlock atl) {
    logmsg(loglevel::debug, "complete call " + seqnr.field());
    txlock(atl).locked([this, call, &doit, res, seqnr] (mutex_t::token txtoken){
            auto &txb(txbuffer(txtoken));
            auto oldavail(txb.avail());
            serialise1 s(txb);
            proto::respheader(-1, seqnr, res).serialise(s);
            doit(s, txtoken);

            auto &config(owner.owner.config);
            auto sz = txb.avail() - oldavail;
            assert(sz < proto::maxmsgsize);
            *txb.linearise<unsigned>(oldavail + txb.offset()) = (unsigned)sz;

            /* Tell the worker that we're finishing.  After we've done
             * this, the worker can exit as soon as we drop the
             * lock. */
            auto oldnroutstanding(outstandingcalls(txtoken).length());
            outstandingcalls(txtoken).drop(*call);

            if (outstandingcalls(txtoken).empty() &&
                pauselock.locked<bool>([this] (mutex_t::token t) {
                        return wantpause(t); })) {
                pausepub.publish(); }

            /* Try a fast synchronous send rather than waking the
             * worker thread.  No point if there was stuff in the
             * buffer before we started: whoever put it there was
             * either the conn thread, in which case the conn thread
             * is about to try a transmit and we don't gain anything
             * by doing another one here, or they weren't, in which
             * case their send must have failed (or the buffer would
             * now be empty) and ours probably would as well. */
            orerror<void> r(Success);
            if (oldavail == 0 && !call->abandoned().ready()) {
                r = txb.sendfast(fd); }
            if (txb.empty()) logmsg(loglevel::debug, "tx fast");
            else {
                logmsg(loglevel::debug,
                       "tx slow: " + r.field() + " " + fields::mk(oldavail)); }
            /* Need to kick if either we've moved the TX to non-empty
             * (because TX sub might be disarmed), or if we've moved
             * sufficiently clear of the RX quota, or if we're
             * shutting down and this is the last call. */
            if ((oldavail == 0 && !txb.empty()) ||
                oldnroutstanding == (config.maxoutstandingcalls * 3 / 4) ||
                (oldavail >= config.txbufferlimit * 3 / 4 &&
                 txb.avail() < config.txbufferlimit * 3 / 4) ||
                (oldnroutstanding == 1 && owner.shutdown.ready())) {
                completedcall.publish(); } });
    delete call; }

/* Marginally faster version for when we're already on the connection
 * thread.  Never need to kick the thread, because we're already on
 * it, and don't need to use atomic ops, because we hold the lock
 * against other completions. */
void
rpcservice2::connworker::complete(
    orerror<void> res,
    const std::function<void (serialise1 &,
                              mutex_t::token,
                              onconnectionthread)> &doit,
    proto::sequencenr seqnr,
    nnp<incompletecall> call,
    acquirestxlock atl,
    onconnectionthread oct) {
    txlock(atl).locked(
        [this, &call, &doit, res, seqnr, oct] (mutex_t::token txtoken) {
            auto &txb(txbuffer(txtoken));
            auto startavail(txb.avail());
            serialise1 s(txb);
            proto::respheader(-1, seqnr, res).serialise(s);
            doit(s, txtoken, oct);
            auto sz = txb.avail() - startavail;
            assert(sz < proto::maxmsgsize);
            *txb.linearise<unsigned>(startavail + txb.offset()) = (unsigned)sz;
            /* No fast transmit: the conn thread will check for TX as
             * soon as we return, and it's not worth the loss of
             * batching to transmit early when we've already paid the
             * scheduling costs. */
            outstandingcalls(txtoken).drop(call);
            if (outstandingcalls(txtoken).empty() &&
                pauselock.locked<bool>([this] (mutex_t::token t) {
                        assert(!paused(t));
                        return wantpause(t); })) {
                pausepub.publish(); } });
    delete &*call; }

/* Process a single incoming message off of the queue.  The caller
 * must have already parsed the message header; this handles the rest.
 * Returns Success normally (including when the application-level
 * called() method indicates an application-level error) or an error
 * if we hit a fatal error which requires the connection to be torn
 * down. */
orerror<void>
rpcservice2::connworker::processmessage(
    clientio io,
    const orerror<peername> &peer,
    const proto::reqheader &hdr,
    bool &donehello,
    deserialise1 &ds,
    onconnectionthread oct) {
    acquirestxlock atl(io);
    if (hdr.vers != version::current) {
        logmsg(loglevel::info,
               "peer " + fields::mk(peer) +
               " requested version " + fields::mk(hdr.vers) +
               "; we only support " + fields::mk(version::current));
        return error::badversion; }
    if (!owner.type.contains(hdr.type)) {
        logmsg(loglevel::info,
               "peer " + fields::mk(peer) +
               " requested interface " + fields::mk(hdr.type) +
               " on a service supporting " +
               fields::mk(owner.type));
        return error::badinterface; }
    if (hdr.type == interfacetype::meta) {
        proto::meta::tag t(ds);
        if (t == proto::meta::tag::hello) {
            if (donehello) {
                logmsg(loglevel::info,
                       "peer " + fields::mk(peer) + " sent multiple HELLOs?");
                return error::toolate; }
            calledhello(hdr.seq, atl, oct);
            logmsg(loglevel::debug, "hello from " + fields::mk(peer));
            donehello = true;
            return Success; }
        else if (t == proto::meta::tag::abort) {
            /* XXX running aborts over the same socket as requests is
             * a little bit dodgy; we should arguably arrange things
             * so that aborts can go through even when the main
             * machine is backed up.  Doing it this way looks really
             * quite deadlock-prone.  That's a bit harder to get
             * right, though, so let's leave it like this for now. */
            if (!donehello) {
                logmsg(loglevel::info,
                       "peer " + fields::mk(peer) +
                       " sent abort before HELLO?");
                return error::toosoon; }
            logmsg(loglevel::debug, "attempt abort " + hdr.seq.field());
            txlock(atl).locked([this, &hdr] (mutex_t::token tok) {
                    for (auto it(outstandingcalls(tok).start());
                         !it.finished();
                         it.next()) {
                        auto i(*it);
                        if (i->seqnr != hdr.seq) continue;
                        if (!i->abandoned(tok).ready()) {
                            logmsg(loglevel::debug, "aborting call");
                            i->abandoned(tok).set(); }
                        else logmsg(loglevel::debug, "double abort?");
                        return; }
                    logmsg(loglevel::debug,
                           "too-late abort on " + hdr.seq.field()); } );
            /* Aborts generate no reply. */
            return Success; }
        else {
            logmsg(loglevel::info,
                   "peer " + fields::mk(peer) +
                   " sent unrecognised meta request");
            return error::invalidmessage; } }
    if (!donehello) {
        logmsg(loglevel::info,
               "peer " + fields::mk(peer) +
               " sent request before sending HELLO?");
        return error::toosoon; }
    if (txlock(atl).locked<bool>([this, &hdr] (mutex_t::token tok) {
                for (auto it(outstandingcalls(tok).start());
                     !it.finished();
                     it.next()) {
                    if ((*it)->seqnr == hdr.seq) return true; }
                return false; })) {
        logmsg(loglevel::info,
               "peer " + fields::mk(peer) +
               " sent duplicate sequence number " +
               hdr.seq.field());
        return error::invalidmessage; }
    logmsg(loglevel::debug, "received call " + hdr.seq.field());
    /* We drop the lock to allocate the call, so something might get
     * removed from the list, but that's fine because it can't cause a
     * non-dupe to become a duplicate.  It's only really a debug
     * check, anyway. */
    auto ic(_nnp(*new incompletecall(*this, hdr.seq)));
    txlock(atl).locked([this, ic] (mutex_t::token tok) {
            outstandingcalls(tok).pushtail(ic); });
    auto res(owner.owner.called(io, ds, hdr.type, ic, oct));
    if (res.isfailure()) ic->fail(res.failure(), atl);
    return Success; }

void
rpcservice2::connworker::calledhello(proto::sequencenr seq,
                                     acquirestxlock atl,
                                     onconnectionthread oct) {
    auto ic(_nnp(*new incompletecall(*this, seq)));
    txlock(atl).locked([this, ic] (mutex_t::token tok) {
            outstandingcalls(tok).pushtail(ic); });
    ic->complete([this]
                 (serialise1 &s,
                  mutex_t::token /* txlock */,
                  onconnectionthread) {
                     /* min */
                     version::current.serialise(s);
                     /* max */
                     version::current.serialise(s);
                     /* types */
                     owner.type.serialise(s); },
                 atl,
                 oct); }

bool
rpcservice2::connworker::quotaavail(acquirestxlock atl) const {
    return txlock(atl).locked<bool>([this] (mutex_t::token tok) {
            return quotaavail(tok); }); }

bool
rpcservice2::connworker::quotaavail(mutex_t::token tok) const {
    auto &config(owner.owner.config);
    return !pauselock.locked<bool>([this] (mutex_t::token t) {
            return wantpause(t); }) &&
        outstandingcalls(tok).length() < config.maxoutstandingcalls &&
        txbuffer(tok).avail() < config.txbufferlimit; }
