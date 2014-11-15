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

#include "list.tmpl"
#include "mutex.tmpl"
#include "test.tmpl"
#include "thread.tmpl"
#include "waitbox.tmpl"

#include "fieldfinal.H"

class rpcservice2::rootthread : public thread {
public: rpcservice2 &owner;
public: listenfd const fd;
public: waitbox<void> shutdown;
public: waitbox<orerror<void> > initialisedone;
public: list<interfacetype> type; /* const once initialised */
public: beaconserver *beacon;
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
      beacon(NULL) {
    assert(!type.contains(interfacetype::meta));
    type.pushtail(interfacetype::meta); }
public: void run(clientio) final; };

class rpcservice2::connworker : public thread {
    /* Shutdown box used by the conn thread to request shutdown of
     * outstanding incomplete calls. */
public: waitbox<void> shutdown;
public: mutex_t _txlock;
public: mutex_t &txlock(acquirestxlock) { return _txlock; }
public: buffer txbuffer;
public: unsigned outstandingcalls;
public: rootthread &owner;
public: socket_t const fd;
public: publisher completedcall;

public: connworker(
    const constoken &token,
    rootthread &_owner,
    socket_t _fd)
    : thread(token),
      shutdown(),
      _txlock(),
      txbuffer(),
      outstandingcalls(0),
      owner(_owner),
      fd(_fd),
      completedcall() {}

public: void run(clientio) final;
public: void complete(
    const std::function<void (serialise1 &, mutex_t::token)> &,
    proto::sequencenr,
    acquirestxlock);
public: void fail(error, proto::sequencenr, acquirestxlock);
public: void complete(
    const std::function<void (serialise1 &,
                              mutex_t::token,
                              onconnectionthread)> &doit,
    proto::sequencenr,
    acquirestxlock,
    onconnectionthread oct);
public: void fail(
    error err,
    proto::sequencenr,
    acquirestxlock,
    onconnectionthread oct);
public: void complete(
    orerror<void>,
    proto::sequencenr,
    acquirestxlock);
public: void complete(
    orerror<void>,
    proto::sequencenr,
    acquirestxlock,
    onconnectionthread);
public: void txcomplete(unsigned long, mutex_t::token);
public: void txcomplete(unsigned long, mutex_t::token, onconnectionthread);
public: orerror<void> calledhello(
        deserialise1 &ds,
        nnp<incompletecall> ic,
        acquirestxlock,
        onconnectionthread); };

rpcservice2config::rpcservice2config(const beaconserverconfig &_beacon,
                                     unsigned _maxoutstandingcalls,
                                     unsigned _txbufferlimit)
    : beacon(_beacon),
      maxoutstandingcalls(_maxoutstandingcalls),
      txbufferlimit(_txbufferlimit) {}

rpcservice2config
rpcservice2config::dflt(const clustername &cn,
                        const slavename &sn) {
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
        destroy(io);
        return beacon.failure(); }
    auto res(initialise(io));
    if (res.isfailure()) {
        root->initialisedone.set(res);
        beacon.success()->destroy(io);
        destroy(io);
        return res; }
    root->beacon = beacon.success();
    root->initialisedone.set(res);
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
    connworker &_owner,
    proto::sequencenr _seqnr)
    : owner(_owner),
      seqnr(_seqnr) {}

const waitbox<void> &
rpcservice2::incompletecall::abandoned() const { return owner.shutdown; }

void
rpcservice2::incompletecall::complete(
    const std::function<void (serialise1 &, mutex_t::token)> &doit,
    acquirestxlock atl) {
    owner.complete(doit, seqnr, atl);
    delete this; }

void
rpcservice2::incompletecall::fail(error e, acquirestxlock atl) {
    owner.fail(e, seqnr, atl);
    delete this; }

void
rpcservice2::incompletecall::complete(
    orerror<void> res,
    acquirestxlock atl) {
    owner.complete(res, seqnr, atl);
    delete this; }

void
rpcservice2::incompletecall::complete(
    const std::function<void (serialise1 &,
                              mutex_t::token,
                              onconnectionthread)> &doit,
    acquirestxlock atl,
    onconnectionthread oct) {
    owner.complete(doit, seqnr, atl, oct);
    delete this; }

void
rpcservice2::incompletecall::fail(
    error e,
    acquirestxlock atl,
    onconnectionthread oct) {
    owner.fail(e, seqnr, atl, oct);
    delete this; }

void
rpcservice2::incompletecall::complete(
    orerror<void> res,
    acquirestxlock atl,
    onconnectionthread oct) {
    owner.complete(res, seqnr, atl, oct);
    delete this; }


rpcservice2::incompletecall::~incompletecall() {}

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
    subscription ss(sub, shutdown.pub);
    list<subscription> workers;
    iosubscription ios(sub, fd.poll());
    while (!shutdown.ready()) {
        auto s(sub.wait(io));
        if (s == &ss) continue;
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

    subscription ss(sub, owner.shutdown.pub);
    subscription completedsub(sub, completedcall);

    buffer rxbuffer;

    auto &config(owner.owner.config);

    /* Shouldn't have stuff to transmit yet, because we've not
     * processed any calls and can't have any responses. */
    assert(txbuffer.empty());

    bool donehello = false;
    bool failed;
    failed = false;

    acquirestxlock atl(io);

    while (!failed && !owner.shutdown.ready()) {
        /* The obvious races here are all handled by re-checking
         * everything when completedsub gets notified. */
        if (!insubarmed &&
            atomicload(outstandingcalls) < config.maxoutstandingcalls &&
            txbuffer.avail() < config.txbufferlimit) {
            insub.rearm();
            insubarmed = true; }
        if (!outsubarmed && !txbuffer.empty()) {
            outsub.rearm();
            outsubarmed = true; }
        subscriptionbase *s;
        bool trysend = false;
        bool tryrecv = false;
        if (rxbuffer.empty() ||
            atomicload(outstandingcalls) >= config.maxoutstandingcalls ||
            txbuffer.avail() >= config.txbufferlimit) {
            s = sub.wait(io); }
        else {
            s = NULL;
            tryrecv = true; }
        if (/* Handled by loop condition */
            s == &ss ||
            /* Handled by the rearm block above sub.wait() */
            s == &completedsub) {
            continue; }
        if (s == &insub || s == &errsub) {
            if (s == &insub) {
                assert(insubarmed);
                insubarmed = false; }
            else {
                /* Use a quick RX check to pick up any errors, and to
                 * filter out spurious wake-ups. */
                errsub.rearm(); }
            {   auto res(rxbuffer.receivefast(fd));
                if (res == error::wouldblock) continue;
                if (res.isfailure()) {
                    /* Connection dead. */
                    failed = true;
                    continue; } }
            tryrecv = true; }
        if (tryrecv) {
            while (atomicload(outstandingcalls)
                       < config.maxoutstandingcalls &&
                   txbuffer.avail() < config.txbufferlimit) {
                deserialise1 ds(rxbuffer);
                proto::reqheader hdr(ds);
                if (ds.status() == error::underflowed) break;
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
                    if (hdr.size > rxbuffer.avail()) break; }
                atomicinc(outstandingcalls);
                auto ic(_nnp(*new incompletecall(*this, hdr.seq)));
                onconnectionthread oct;
                orerror<void> res(Success);

                if (hdr.vers != version::current) {
                    logmsg(loglevel::info,
                           "peer " + fields::mk(peer) +
                           " requested version " + fields::mk(hdr.vers) +
                           "; we only support " + fields::mk(version::current));
                    res = error::badversion; }
                else if (!owner.type.contains(hdr.type)) {
                    logmsg(loglevel::info,
                           "peer " + fields::mk(peer) +
                           " requested interface " + fields::mk(hdr.type) +
                           " on a service supporting " +
                           fields::mk(owner.type));
                    res = error::badinterface; }
                else if (hdr.type == interfacetype::meta) {
                    if (donehello) {
                        logmsg(loglevel::info,
                               "peer " + fields::mk(peer) +
                               " sent mulitple HELLOs");
                        res = error::toolate; }
                    else {
                        res = calledhello(ds, ic, atl, oct);
                        donehello = true; } }
                else if (!donehello) {
                    logmsg(loglevel::info,
                           "peer " + fields::mk(peer) +
                           " sent request before sending HELLO?");
                    res = error::toosoon; }
                else res = owner.owner.called(io, ds, hdr.type, ic, oct);

                if (res.isfailure()) ic->fail(res.failure(), atl);
                else if (ds.offset() != rxbuffer.offset() + hdr.size) {
                    logmsg(loglevel::error,
                           "expected message to go from " +
                           fields::mk(rxbuffer.offset()) + " to " +
                           fields::mk(rxbuffer.offset() + hdr.size) +
                           "; actually went to " +
                           fields::mk(ds.offset()));
                    failed = true;
                    break; }
                rxbuffer.discard(hdr.size); }
            trysend = true; }
        if (s == &outsub) {
            assert(outsubarmed);
            outsubarmed = false;
            trysend = true; }
        if (trysend && !txbuffer.empty()) {
            auto res(txlock(atl).locked<orerror<void> >([this] {
                        return txbuffer.sendfast(fd); }));
            if (res.isfailure() && res != error::wouldblock) failed = true; } }
    /* Tell outstanding calls to abort.  Under the TX lock to avoid a
     * race with fast TX. */
    txlock(atl).locked([this] { shutdown.set(); });
    /* Wait for them to finish. */
    {   auto laststatus(timestamp::now());
        subscriber smallsub;
        subscription c(smallsub, completedcall);
        while (atomicload(outstandingcalls) > 0) {
            if (smallsub.wait(io, laststatus + timedelta::seconds(1)) == NULL) {
                logmsg(loglevel::info,
                       "waiting to shut down service to " + fields::mk(peer) +
                       "; " + fields::mk(atomicload(outstandingcalls)) +
                       " left");
                laststatus = timestamp::now(); } } }
    /* We're done */
    fd.close(); }

void
rpcservice2::connworker::complete(
    const std::function<void (serialise1 &, mutex_t::token)> &doit,
    proto::sequencenr seqnr,
    acquirestxlock atl) {
    txlock(atl).locked([this, &doit, seqnr] (mutex_t::token txtoken) {
            auto oldavail(txbuffer.avail());
            serialise1 s(txbuffer);
            proto::respheader(-1, seqnr, Success).serialise(s);
            doit(s, txtoken);
            txcomplete(oldavail, txtoken); }); }

void
rpcservice2::connworker::fail(
    error err,
    proto::sequencenr seqnr,
    acquirestxlock atl) {
    txlock(atl).locked([this, err, seqnr] (mutex_t::token txtoken) {
            auto oldavail(txbuffer.avail());
            serialise1 s(txbuffer);
            proto::respheader(-1, seqnr, err).serialise(s);
            txcomplete(oldavail, txtoken); }); }

void
rpcservice2::connworker::complete(
    orerror<void> res,
    proto::sequencenr seqnr,
    acquirestxlock atl) {
    if (res.isfailure()) fail(res.failure(), seqnr, atl);
    else complete([] (serialise1 &, mutex_t::token) {}, seqnr, atl); }

/* Marginally faster version for when we're already on the connection
 * thread.  Never need to kick the thread, because we're already on
 * it, and don't need to use atomic ops, because we hold the lock
 * against other completions. */
void
rpcservice2::connworker::complete(
    const std::function<void (serialise1 &,
                              mutex_t::token,
                              onconnectionthread)> &doit,
    proto::sequencenr seqnr,
    acquirestxlock atl,
    onconnectionthread oct) {
    txlock(atl).locked([this, &doit, seqnr, oct] (mutex_t::token txtoken) {
            auto startavail(txbuffer.avail());
            serialise1 s(txbuffer);
            proto::respheader(-1, seqnr, Success).serialise(s);
            doit(s, txtoken, oct);
            txcomplete(startavail, txtoken, oct); }); }

void
rpcservice2::connworker::fail(error err,
                              proto::sequencenr seqnr,
                              acquirestxlock atl,
                              onconnectionthread oct) {
    txlock(atl).locked([this, err, seqnr, oct] (mutex_t::token txtoken) {
            auto startavail(txbuffer.avail());
            serialise1 s(txbuffer);
            proto::respheader(-1, seqnr, err).serialise(s);
            txcomplete(startavail, txtoken, oct); }); }

void
rpcservice2::connworker::complete(orerror<void> res,
                                  proto::sequencenr seqnr,
                                  acquirestxlock atl,
                                  onconnectionthread oct) {
    if (res.isfailure()) fail(res.failure(), seqnr, atl, oct);
    else complete(
        [] (serialise1 &, mutex_t::token, onconnectionthread) {},
        seqnr,
        atl,
        oct); }

/* Tail end of transmitting a reply.  Set the size in the reply,
 * remove it from the outstandingcalls quota, try a fast send, and
 * kick the connection thread, if necessary.  There's another variant
 * for when we're already on the conn thread. */
void
rpcservice2::connworker::txcomplete(unsigned long oldavail,
                                    mutex_t::token /* txlock */) {
    auto &config(owner.owner.config);
    auto sz = txbuffer.avail() - oldavail;
    assert(sz < proto::maxmsgsize);
    *txbuffer.linearise<unsigned>(oldavail + txbuffer.offset()) = (unsigned)sz;

    auto oldnroutstanding(atomicloaddec(outstandingcalls));

    /* Try a fast synchronous send rather than waking the worker
     * thread.  No point if there was stuff in the buffer before we
     * started: whoever put it there was either the conn thread, in
     * which case the conn thread is about to try a transmit and we
     * don't gain anything by doing another one here, or they weren't,
     * in which case their send must have failed (or the buffer would
     * now be empty) and ours probably would as well. */
    if (oldavail == 0 && !shutdown.ready()) (void)txbuffer.sendfast(fd);
    /* Need to kick if either we've moved the TX to non-empty (because
     * TX sub might be disarmed), or if we've moved sufficiently clear
     * of the RX quota, or if we're shutting down and this is the last
     * call. */
    if ((oldavail == 0 && !txbuffer.empty()) ||
        oldnroutstanding == (config.maxoutstandingcalls * 3 / 4) ||
        (oldavail >= config.txbufferlimit * 3 / 4 &&
         txbuffer.avail() < config.txbufferlimit * 3 / 4) ||
        (oldnroutstanding == 1 && owner.shutdown.ready())) {
        completedcall.publish(); } }

/* txcomplete() specialised for when we already happen to be on the
 * conn thread. */
void
rpcservice2::connworker::txcomplete(unsigned long oldavail,
                                    mutex_t::token /* txlock */,
                                    onconnectionthread) {
    auto sz = txbuffer.avail() - oldavail;
    assert(sz < proto::maxmsgsize);
    *txbuffer.linearise<unsigned>(oldavail + txbuffer.offset()) = (unsigned)sz;
    /* No fast transmit: the conn thread will check for TX as soon as
     * we return, and it's not worth the loss of batching to transmit
     * early when we've already paid the scheduling costs. */
    /* Not sync: only the conn thread and holders of the TX lock touch
     * it, and we're on the conn thread and hold the lock, so can't
     * race. */
    outstandingcalls--; }

orerror<void>
rpcservice2::connworker::calledhello(deserialise1 &ds,
                                     nnp<incompletecall> ic,
                                     acquirestxlock atl,
                                     onconnectionthread oct) {
    proto::hello::req h(ds);
    if (ds.isfailure()) ds.failure().warn("parsing HELLO request");
    else {
        ic->complete([this]
                     (serialise1 &s,
                      mutex_t::token /* txlock */,
                      onconnectionthread) {
                         proto::hello::resp(version::current,
                                            version::current,
                                            owner.type)
                             .serialise(s); },
                     atl,
                     oct); }
    return ds.status(); }
