#include "rpcservice2.H"

#include <sys/socket.h>
#include <unistd.h>

#include "fields.H"
#include "logging.H"
#include "proto2.H"
#include "serialise.H"
#include "thread.H"
#include "timedelta.H"
#include "util.H"

#include "list.tmpl"
#include "mutex.tmpl"
#include "thread.tmpl"

#include "fieldfinal.H"

class rpcservice2::rootthread : public thread {
public: rpcservice2 &owner;
public: listenfd const fd;
public: waitbox<void> shutdown;
public: rootthread(const constoken &token, rpcservice2 &_owner, listenfd _fd)
    : thread(token),
      owner(_owner),
      fd(_fd),
      shutdown() {}
public: void run(clientio) final; };

class rpcservice2::connworker : public thread {
public: waitbox<void> shutdown;
public: mutex_t txlock;
public: buffer txbuffer;
public: unsigned outstandingcalls;
public: rootthread &owner;
public: socket_t const fd;
public: publisher completedcall;

public: connworker(const constoken &token, rootthread &_owner, socket_t _fd)
    : thread(token),
      shutdown(),
      txlock(),
      txbuffer(),
      outstandingcalls(0),
      owner(_owner),
      fd(_fd),
      completedcall() {}

public: void run(clientio) final;
public: void complete(
    const std::function<void (serialise1 &, mutex_t::token)> &,
    proto::sequencenr);
public: void fail(error, proto::sequencenr);
public: void complete(
    const std::function<void (serialise1 &,
                              mutex_t::token,
                              onconnectionthread)> &doit,
    onconnectionthread oct,
    proto::sequencenr);
public: void fail(error err, onconnectionthread oct, proto::sequencenr);
public: void txcomplete(unsigned long, mutex_t::token);
public: void txcomplete(unsigned long, mutex_t::token, onconnectionthread);
public: orerror<void> calledhello(
        clientio,
        onconnectionthread,
        deserialise1 &ds,
        nnp<incompletecall> ic); };

rpcservice2config::rpcservice2config(unsigned _maxoutstandingcalls,
                                     unsigned _txbufferlimit)
    : maxoutstandingcalls(_maxoutstandingcalls),
      txbufferlimit(_txbufferlimit) {}

rpcservice2config
rpcservice2config::dflt() {
    return rpcservice2config(
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
    else if (::bind(s, pn.sockaddr(), pn.sockaddrsize()) < 0 ||
             ::listen(s, 10) < 0) {
        ::close(s);
        return error::from_errno(); }
    else return listenfd(s); }

rpcservice2::rpcservice2(const constoken &ct)
    : config(ct.config),
      root(thread::start<rootthread>(
               "R:" + fields::mk(ct.pn),
               *this,
               ct.fd)) {}

peername::port
rpcservice2::port() const { return root->fd.localname().getport(); }

rpcservice2::onconnectionthread::onconnectionthread() {}

const waitbox<void> &
rpcservice2::incompletecall::abandoned() const { return owner.shutdown; }

rpcservice2::incompletecall::incompletecall(
    connworker &_owner,
    proto::sequencenr _seqnr)
    : owner(_owner),
      seqnr(_seqnr) {}

void
rpcservice2::incompletecall::complete(
    const std::function<void (serialise1 &, mutex_t::token)> &doit) {
    owner.complete(doit, seqnr);
    delete this; }

void
rpcservice2::incompletecall::complete(
    const std::function<void (serialise1 &,
                              mutex_t::token,
                              onconnectionthread)> &doit,
    onconnectionthread oct) {
    owner.complete(doit, oct, seqnr);
    delete this; }

void
rpcservice2::incompletecall::fail(error e) {
    owner.fail(e, seqnr);
    delete this; }

void
rpcservice2::incompletecall::fail(error e, onconnectionthread oct) {
    owner.fail(e, oct, seqnr);
    delete this; }

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
     * started: whoever put it there would have tried a sync send when
     * they did it and that must have failed, so our sync send will
     * almost certainly also fail. */
    if (oldavail == 0) (void)txbuffer.sendfast(fd);
    /* Need to kick if either we've moved the TX to non-empty (because
     * TX sub might be disarmed) or if we've moved sufficiently clear
     * of the RX quota. */
    if ((oldavail == 0 && !txbuffer.empty()) ||
        oldnroutstanding == (config.maxoutstandingcalls * 3 / 4) ||
        (oldavail >= config.txbufferlimit * 3 / 4 &&
         txbuffer.avail() < config.txbufferlimit * 3 / 4) ||
        oldnroutstanding == 1) {
        completedcall.publish(); } }

void
rpcservice2::connworker::complete(
    const std::function<void (serialise1 &, mutex_t::token)> &doit,
    proto::sequencenr seqnr) {
    txlock.locked([this, &doit, seqnr] (mutex_t::token txtoken) {
            auto oldavail(txbuffer.avail());
            serialise1 s(txbuffer);
            proto::respheader(-1, seqnr, Success).serialise(s);
            doit(s, txtoken);
            txcomplete(oldavail, txtoken); }); }

void
rpcservice2::connworker::fail(error err, proto::sequencenr seqnr) {
    txlock.locked([this, err, seqnr] (mutex_t::token txtoken) {
            auto oldavail(txbuffer.avail());
            serialise1 s(txbuffer);
            proto::respheader(-1, seqnr, err).serialise(s);
            txcomplete(oldavail, txtoken); }); }

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

/* Marginally faster version for when we're already on the connection
 * thread.  Never need to kick the thread, because we're already on
 * it, and don't need to use atomic ops, because we hold the lock
 * against other completions. */
void
rpcservice2::connworker::complete(
    const std::function<void (serialise1 &,
                              mutex_t::token,
                              onconnectionthread)> &doit,
    onconnectionthread oct,
    proto::sequencenr seqnr) {
    txlock.locked([this, &doit, seqnr, oct] (mutex_t::token txtoken) {
            auto startavail(txbuffer.avail());
            serialise1 s(txbuffer);
            proto::respheader(-1, seqnr, Success).serialise(s);
            doit(s, txtoken, oct);
            txcomplete(startavail, txtoken, oct); }); }

void
rpcservice2::connworker::fail(error err,
                              onconnectionthread oct,
                              proto::sequencenr seqnr) {
    txlock.locked([this, err, seqnr, oct] (mutex_t::token txtoken) {
            auto startavail(txbuffer.avail());
            serialise1 s(txbuffer);
            proto::respheader(-1, seqnr, err).serialise(s);
            txcomplete(startavail, txtoken, oct); }); }

void
rpcservice2::rootthread::run(clientio io) {
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
            workers.append(sub, r->pub(), r); }
        else {
            /* One of our worker threads died. */
            auto died(static_cast<connworker *>(s->data));
            auto death(died->hasdied());
            if (death == Nothing) continue;
            for (auto it(workers.start()); true; it.next()) {
                if (it->data == died) {
                    it.remove();
                    break; } };
            died->join(death.just()); } }
    /* Time to die.  Wait for all of the worker threads to go away.
     * Should be reasonably quick because they all watch the same
     * shutdown box. */
    while (!workers.empty()) {
        auto it(workers.start());
        auto w( (connworker *)it->data );
        it.remove();
        w->join(io); }
    fd.close(); }

orerror<void>
rpcservice2::connworker::calledhello(clientio,
                                     onconnectionthread oct,
                                     deserialise1 &ds,
                                     nnp<incompletecall> ic) {
    proto::hello::req h(ds);
    if (ds.issuccess()) {
        ic->complete([] (serialise1 &s,
                         mutex_t::token /* txlock */,
                         onconnectionthread) {
                         proto::hello::resp(version::current,
                                            version::current)
                             .serialise(s); },
                     oct); }
    return ds.status(); }

void
rpcservice2::connworker::run(clientio io) {
    subscriber sub;
    /* We keep the in subscription armed unless we're trying to get
     * the other side to back off (either because we have too many
     * outstanding calls or because the TX buffer is too big). */
    iosubscription insub(sub, fd.poll(POLLIN));
    bool insubarmed = true;

    /* Out subscription is armed whenever we have stuff in the TX
     * buffer. */
    iosubscription outsub(sub, fd.poll(POLLOUT));
    bool outsubarmed = true;

    /* Always watch for errors and EOF. */
    iosubscription errsub(sub, fd.poll(POLLERR));

    subscription ss(sub, owner.shutdown.pub);
    subscription completedsub(sub, completedcall);

    buffer rxbuffer;

    auto &config(owner.owner.config);

    bool donehello = false;
    bool failed;
    failed = false;
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
        auto s(sub.wait(io));
        bool trysend = false;
        if (/* Handled by loop condition */
            s == &ss ||
            /* Handled by the rearm block above sub.wait() */
            s == &completedsub) {
            continue; }
        /* errsub handling is really a subset of insub handling, but
         * it doesn't do any harm to always do both. */
        else if (s == &insub || s == &errsub) {
            if (s == &insub) {
                assert(insubarmed);
                insubarmed = false; }
            else errsub.rearm();
            {   auto res(rxbuffer.receivefast(fd));
                if (res == error::wouldblock) continue;
                if (res.isfailure()) {
                    /* Connection dead. */
                    failed = true;
                    continue; } }
            while (atomicload(outstandingcalls)
                       < config.maxoutstandingcalls &&
                   txbuffer.avail() < config.txbufferlimit) {
                if (rxbuffer.avail() < sizeof(proto::reqheader)) break;
                deserialise1 ds(rxbuffer);
                proto::reqheader hdr(ds);
                if (ds.isfailure() ||
                    hdr.size < sizeof(hdr) ||
                    hdr.size > proto::maxmsgsize) {
                    failed = true;
                    break; }
                if (hdr.size > rxbuffer.avail()) break;
                atomicinc(outstandingcalls);
                auto ic(_nnp(*new incompletecall(*this, hdr.seq)));
                onconnectionthread oct;
                orerror<void> res(Success);
                if (hdr.vers != version::current) res = error::badversion;
                else if (donehello) res = owner.owner.called(io, oct, ds, ic);
                else res = calledhello(io, oct, ds, ic);
                donehello = true;
                if (res.isfailure()) {
                    ic->fail(res.failure());
                    continue; }
                else if (ds.offset() != rxbuffer.offset() + hdr.size) {
                    logmsg(loglevel::error,
                           "expected message to go from " +
                           fields::mk(rxbuffer.offset()) + " to " +
                           fields::mk(rxbuffer.offset() + hdr.size) +
                           "; actually went to " +
                           fields::mk(ds.offset()));
                    failed = true;
                    break; }
                else rxbuffer.discard(ds.offset() - rxbuffer.offset()); }
            trysend = true; }
        if (s == &outsub) {
            assert(outsubarmed);
            outsubarmed = false;
            trysend = true; }
        if (trysend && !txbuffer.empty()) {
            auto res(txlock.locked<orerror<void> >([this] {
                        return txbuffer.sendfast(fd); }));
            if (res.isfailure() && res != error::wouldblock) failed = true; } }
    /* Tell outstanding calls to abort. */
    shutdown.set();
    /* Wait for them to finish. */
    {   auto laststatus(timestamp::now());
        subscriber smallsub;
        subscription c(smallsub, completedcall);
        while (atomicload(outstandingcalls) > 0) {
            if (smallsub.wait(io, laststatus + timedelta::seconds(1)) == NULL) {
                logmsg(loglevel::info,
                       "waiting to shut down service to " +
                       fields::mk(fd.peer()) +
                       "; " +
                       fields::mk(atomicload(outstandingcalls)) +
                       " left");
                laststatus = timestamp::now(); } } }
    /* We're done */
    fd.close(); }

void
rpcservice2::destroy(clientio io) {
    root->shutdown.set();
    root->join(io);
    delete this; }

rpcservice2::~rpcservice2() {}
