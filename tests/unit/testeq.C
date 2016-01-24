#include "eqclient.H"
#include "eqserver.H"
#include "rpcservice2.H"
#include "test2.H"
#include "testassert.H"

#include "either.tmpl"
#include "fields.tmpl"
#include "orerror.tmpl"
#include "pair.tmpl"
#include "parsers.tmpl"
#include "rpcservice2.tmpl"
#include "test.tmpl"
#include "test2.tmpl"
#include "testassert.tmpl"
#include "timedelta.tmpl"

class eqtestserver : public rpcservice2 {
public: eqserver &eqs;
public: eqtestserver(const constoken &tok, eqserver &_eqs)
    : rpcservice2(tok,
                  list<interfacetype>::mk(interfacetype::test,
                                          interfacetype::eq)),
      eqs(_eqs) {}
public: orerror<void> called(clientio io,
                             deserialise1 &ds,
                             interfacetype typ,
                             nnp<incompletecall> ic,
                             onconnectionthread oct) {
    assert(typ == interfacetype::eq);
    return eqs.called(io, ds, ic, oct); } };

class eqslowtestserver : public rpcservice2 {
public: eqserver &eqs;
public: waitbox<void> &connstarted;
public: waitbox<void> &connresume;
public: eqslowtestserver(const constoken &tok,
                         eqserver &_eqs,
                         waitbox<void> &_connstarted,
                         waitbox<void> &_connresume)
    : rpcservice2(tok,
                  list<interfacetype>::mk(interfacetype::test,
                                          interfacetype::eq)),
      eqs(_eqs),
      connstarted(_connstarted),
      connresume(_connresume) {}
public: orerror<void> called(clientio io,
                             deserialise1 &ds,
                             interfacetype typ,
                             nnp<incompletecall> ic,
                             onconnectionthread oct) {
    assert(typ == interfacetype::eq);
    if (!connstarted.ready()) connstarted.set();
    connresume.get(io);
    return eqs.called(io, ds, ic, oct); } };

class dummyservice : public rpcservice2 {
public: dummyservice(const constoken &tok)
    : rpcservice2(tok, interfacetype::eq) {}
public: orerror<void> called(clientio,
                             deserialise1 &,
                             interfacetype t,
                             nnp<incompletecall>,
                             onconnectionthread) {
    assert(t == interfacetype::eq);
    return error::dlopen; } };

static void
eqtestcase(clientio io,
           const std::function<void (clientio,
                                     eqclient<unsigned> &,
                                     eventqueue<unsigned> &)> &f,
           const eventqueueconfig &qconf = eventqueueconfig::dflt(),
           const eqclientconfig &qcconfig = eqclientconfig::dflt()) {
    quickcheck qc;
    auto cn(mkrandom<clustername>(qc));
    agentname sn(qc);
    auto pool(connpool::build(cn).fatal("starting conn pool"));
    auto server(eqserver::build());
    auto s(rpcservice2::listen<eqtestserver>(
               io,
               cn,
               sn,
               peername::all(peername::port::any),
               *server)
           .fatal("starting service"));
    filename statefile("S");
    statefile.unlink();
    eqserver::formatqueue(proto::eq::names::testunsigned, statefile, qconf)
        .fatal("formating test queue");
    auto q(server->openqueue(proto::eq::names::testunsigned, statefile)
           .fatal("opening test queue"));
    auto c(eqclient<unsigned>::connect(
               io,
               *pool,
               sn,
               proto::eq::names::testunsigned,
               timestamp::now() + timedelta::seconds(10),
               qcconfig)
           .fatal("connecting eqclient")
           .first());
    f(io, *c, *q);
    c->destroy();
    pool->destroy();
    assert(timedelta::time([&] { s->destroy(io); })
           < timedelta::milliseconds(200));
    assert(timedelta::time([&] { server->destroy(); })
           < timedelta::milliseconds(200));
    assert(timedelta::time([&] {
                q->destroy(rpcservice2::acquirestxlock(io)); })
        < timedelta::milliseconds(100));
    statefile.unlink().fatal("unlinking test queue state file"); }

static testmodule __testeq(
    "eq",
    list<filename>::mk("eq.C",
                       "eq.H",
                       "eqclient.C",
                       "eqclient.H",
                       "eqserver.C",
                       "eqserver.H"),
    testmodule::BranchCoverage(67_pc),
    testmodule::LineCoverage(93_pc),
    "basic", [] (clientio io) {
        eqtestcase(
            io,
            [] (clientio _io,
                eqclient<unsigned> &c,
                eventqueue<unsigned> &q) {
                assert(c.pop() == Nothing);
                q.queue(52, rpcservice2::acquirestxlock(_io));
                assert(timedelta::time([&] { assert(c.pop(_io) == 52); })
                       < timedelta::milliseconds(500));
                q.queue(53, rpcservice2::acquirestxlock(_io));
                assert(timedelta::time([&] { assert(c.pop(_io) == 53); })
                       < timedelta::milliseconds(500));
                assert(c.pop() == Nothing); }); },
    "overflow", [] (clientio io) {
        auto qconf(eventqueueconfig::dflt());
        qconf.queuelimit = 2;
        auto qcconf(eqclientconfig::dflt());
        qcconf.maxqueue = 2;
        eqtestcase(
            io,
            [] (clientio _io,
                eqclient<unsigned> &c,
                eventqueue<unsigned> &q) {
                q.queue(52, rpcservice2::acquirestxlock(_io));
                q.queue(53, rpcservice2::acquirestxlock(_io));
                assert(timedelta::time([&] { assert(c.pop(_io) == 52); })
                       < timedelta::milliseconds(200));
                assert(timedelta::time([&] { assert(c.pop(_io) == 53); })
                       < timedelta::milliseconds(200));
                q.queue(1, rpcservice2::acquirestxlock(_io));
                assert(timedelta::time([&] { assert(c.pop(_io) == 1); })
                       < timedelta::milliseconds(200));
                q.queue(2, rpcservice2::acquirestxlock(_io));
                q.queue(3, rpcservice2::acquirestxlock(_io));
                q.queue(4, rpcservice2::acquirestxlock(_io));
                q.queue(5, rpcservice2::acquirestxlock(_io));
                list<orerror<unsigned> > results;
                for (auto x = 0; x < 4; x++) {
                    auto td(timedelta::time(
                                [&] { results.pushtail(c.pop(_io)); }));
                    tassert(T(td) < T(200_ms));
                    tassert(T(results.peektail().isfailure()) ||
                            T(results.peektail()) == T(x + 2)); }
                tassert(T(results.peektail()) == T(error::eventsdropped));
                assert(c.pop().just() == error::eventsdropped); },
            qconf,
            qcconf); },
    "multiclient", [] (clientio io) {
        auto qconf(eventqueueconfig::dflt());
        qconf.queuelimit = 2;
        quickcheck qc;
        auto cn(mkrandom<clustername>(qc));
        agentname sn(qc);
        auto pool(connpool::build(cn)
                  .fatal("starting conn pool"));
        auto server(eqserver::build());
        auto s(rpcservice2::listen<eqtestserver>(
                   io,
                   cn,
                   sn,
                   peername::all(peername::port::any),
                   *server)
               .fatal("starting service"));
        filename state("S");
        state.unlink();
        eqserver::formatqueue(
            proto::eq::names::testunsigned,
            state,
            qconf)
            .fatal("formating test queue");
        auto q(server->openqueue(proto::eq::names::testunsigned, state)
               .fatal("opening test queue"));
        auto cconfig1(eqclientconfig::dflt());
        cconfig1.get = timedelta::milliseconds(300);
        cconfig1.wait = timedelta::milliseconds(300);
        auto c1(eqclient<unsigned>::connect(
                    io,
                    *pool,
                    sn,
                    proto::eq::names::testunsigned,
                    timedelta::seconds(3).future(),
                    cconfig1)
                .fatal("connecting eqclient")
                .first());
        auto cconfig2(cconfig1);
        cconfig2.maxqueue = 2;
        auto c2(eqclient<unsigned>::connect(
                    io,
                    *pool,
                    sn,
                    proto::eq::names::testunsigned,
                    timedelta::seconds(3).future(),
                    cconfig2)
                .fatal("connecting eqclient")
                .first());
        assert(c1->pop() == Nothing);
        q->queue(1, rpcservice2::acquirestxlock(io));
        assert(c1->pop(io) == 1);
        assert(c2->pop(io) == 1);
        q->queue(2, rpcservice2::acquirestxlock(io));
        assert(c1->pop(io) == 2);
        q->queue(3, rpcservice2::acquirestxlock(io));
        assert(c1->pop(io) == 3);
        assert(c2->pop(io) == 2);
        assert(c2->pop(io) == 3);
        q->queue(4, rpcservice2::acquirestxlock(io));
        q->queue(5, rpcservice2::acquirestxlock(io));
        assert(c1->pop(io) == 4);
        q->queue(6, rpcservice2::acquirestxlock(io));
        assert(c1->pop(io) == 5);
        q->queue(7, rpcservice2::acquirestxlock(io));
        assert(c1->pop(io) == 6);
        q->queue(8, rpcservice2::acquirestxlock(io));
        assert(c1->pop(io) == 7);
        q->queue(9, rpcservice2::acquirestxlock(io));
        assert(c1->pop(io) == 8);
        q->queue(10, rpcservice2::acquirestxlock(io));
        assert(c1->pop(io) == 9);
        q->queue(11, rpcservice2::acquirestxlock(io));
        /* That's enough to overflow the c2 queue.  Depending on
         * how we race with the queue threads we might still get a
         * couple of messages, but something should definitely get
         * dropped. */
        {   auto a(c2->pop(io));
            assert(a == 4 || a == error::eventsdropped); }
        {   auto a(c2->pop(io));
            assert(a == 5 || a == error::eventsdropped); }
        {   auto a(c2->pop(io));
            assert(a == 6 || a == error::eventsdropped); }
        assert(c2->pop(io) == error::eventsdropped);
        /* c1 should be unaffected. */
        assert(c1->pop(io) == 10);
        assert(c1->pop(io) == 11);
        c2->destroy();
        c2 = eqclient<unsigned>::connect(
            io,
            *pool,
            sn,
            proto::eq::names::testunsigned,
            timedelta::seconds(3).future(),
            cconfig2)
            .fatal("connecting eqclient")
            .first();
        assert(c2->pop() == Nothing);
        q->queue(99, rpcservice2::acquirestxlock(io));
        assert(c2->pop(io) == 99);
        assert(c1->pop(io) == 99);
        q->destroy(rpcservice2::acquirestxlock(io));
        assert(c1->pop(io) == error::badqueue);
        s->destroy(io);
        server->destroy();
        {   /* The error we get here depends on how the teardown
             * races against the queue thread: badqueue if the
             * thread can connect to the server after the queue is
             * destroyed, or timeout if it misses the window. */
            auto e(c2->pop(io));
            assert(e == error::badqueue || e == error::timeout); }
        c1->destroy();
        c2->destroy();
        pool->destroy();
        state.unlink().fatal("unlinking test queue"); },
    "badconn", [] (clientio io) {
        quickcheck qc;
        auto cn(mkrandom<clustername>(qc));
        agentname sn(qc);
        auto pool(connpool::build(cn)
                  .fatal("starting conn pool"));
        auto t(timedelta::time([&] {
                    assert(eqclient<unsigned>::connect(
                               io,
                               *pool,
                               sn,
                               proto::eq::names::testunsigned,
                               timedelta::milliseconds(100).future()) ==
                           error::timeout); }));
        assert(t >= timedelta::milliseconds(100));
        assert(t < timedelta::milliseconds(300));
        pool->destroy(); },
    "failwaiter1", [] (clientio io) {
        quickcheck qc;
        auto cn(mkrandom<clustername>(qc));
        agentname sn(qc);
        auto pool(connpool::build(cn).fatal("starting conn pool"));
        auto server(eqserver::build());
        auto s(rpcservice2::listen<eqtestserver>(
                   io,
                   cn,
                   sn,
                   peername::all(peername::port::any),
                   *server)
               .fatal("starting service"));
        filename state("S");
        state.unlink();
        eqserver::formatqueue(proto::eq::names::testunsigned, state)
            .fatal("formating test queue");
        auto q(server->openqueue(proto::eq::names::testunsigned, state)
               .fatal("opening test queue"));
        waitbox<void> startedwaiter;
        tests::hook<void> h(geneqclient::startingwaiter,
                            [&startedwaiter] {
                                startedwaiter.set(); });
        auto cconfig(eqclientconfig::dflt());
        cconfig.get = timedelta::milliseconds(300);
        cconfig.wait = timedelta::milliseconds(300);
        auto c(eqclient<unsigned>::connect(
                   io,
                   *pool,
                   sn,
                   proto::eq::names::testunsigned,
                   timedelta::seconds(3).future(),
                   cconfig)
               .fatal("connecting eqclient")
               .first());
        startedwaiter.get(io);
        s->destroy(io);
        q->destroy(io);
        server->destroy();
        auto t(timedelta::time(
                   [&] { assert(c->pop(io) == error::timeout); }));
        tassert(T(t) >= T(cconfig.wait));
        tassert(T(t) <=
                T(cconfig.get + cconfig.wait + 500_ms));
        c->destroy();
        pool->destroy();
        state.unlink().fatal("unlinking test queue"); },
    "failwaiter2", [] (clientio io) {
        quickcheck qc;
        auto cn(mkrandom<clustername>(qc));
        agentname sn(qc);
        auto pool(connpool::build(cn).fatal("starting conn pool"));
        auto server(eqserver::build());
        auto s(rpcservice2::listen<eqtestserver>(
                   io,
                   cn,
                   sn,
                   peername::all(peername::port::any),
                   *server)
               .fatal("starting service"));
        filename state("S");
        state.unlink();
        eqserver::formatqueue(proto::eq::names::testunsigned, state)
            .fatal("formating test queue");
        auto q(server->openqueue(proto::eq::names::testunsigned, state)
               .fatal("opening test queue"));
        waitbox<void> startedwaiter;
        tests::hook<void> h(geneqclient::startingwaiter,
                            [&startedwaiter] { startedwaiter.set(); });
        auto cconfig(eqclientconfig::dflt());
        cconfig.get = timedelta::milliseconds(300);
        cconfig.wait = timedelta::milliseconds(300);
        auto c(eqclient<unsigned>::connect(
                   io,
                   *pool,
                   sn,
                   proto::eq::names::testunsigned,
                   timedelta::seconds(3).future(),
                   cconfig)
               .fatal("connecting eqclient")
               .first());
        startedwaiter.get(io);
        q->destroy(io);
        auto t(timedelta::time(
                   [&] { assert(c->pop(io) == error::badqueue); }));
        assert(t <= cconfig.wait + timedelta::milliseconds(300));
        s->destroy(io);
        server->destroy();
        c->destroy();
        pool->destroy();
        state.unlink().fatal("unlinking test queue"); },
    "pushlostsub", [] (clientio io) {
        quickcheck qc;
        auto cn(mkrandom<clustername>(qc));
        agentname sn(qc);
        auto pool(connpool::build(cn).fatal("starting conn pool"));
        auto server(eqserver::build());
        auto s(rpcservice2::listen<eqtestserver>(
                   io,
                   cn,
                   sn,
                   peername::all(peername::port::any),
                   *server)
               .fatal("starting service"));
        filename state("S");
        state.unlink();
        eqserver::formatqueue(proto::eq::names::testunsigned, state)
            .fatal("formating test queue");
        auto q(server->openqueue(proto::eq::names::testunsigned, state)
               .fatal("opening test queue"));
        waitbox<void> startedwaiter;
        tests::hook<void> h(geneqclient::startingwaiter,
                            [&startedwaiter] { startedwaiter.set(); });
        auto cconfig(eqclientconfig::dflt());
        cconfig.get = timedelta::milliseconds(300);
        cconfig.wait = timedelta::milliseconds(300);
        auto c(&*eqclient<unsigned>::connect(
                   io,
                   *pool,
                   sn,
                   proto::eq::names::testunsigned,
                   timedelta::seconds(3).future(),
                   cconfig)
               .fatal("connecting eqclient")
               .first());
        /* Make sure it's properly subscribed. */
        startedwaiter.get(io);
        /* Arrange to unsubscribe while the server is halfway
         * through pushing a new event. */
        tests::hook<void> h2(geneventqueue::finishingpush,
                             [&] {
                                 c->destroy();
                                 c = NULL; });
        q->queue(1, rpcservice2::acquirestxlock(io));
        assert(c == NULL);
        pool->destroy();
        s->destroy(io);
        q->destroy(io);
        server->destroy();
        state.unlink().fatal("unlinking test queue"); },
    "shutdownwaiter", [] (clientio io) {
        quickcheck qc;
        auto cn(mkrandom<clustername>(qc));
        agentname sn(qc);
        auto pool(connpool::build(cn).fatal("starting conn pool"));
        auto server(eqserver::build());
        auto s(rpcservice2::listen<eqtestserver>(
                   io,
                   cn,
                   sn,
                   peername::all(peername::port::any),
                   *server)
               .fatal("starting service"));
        filename state("S");
        state.unlink();
        eqserver::formatqueue(proto::eq::names::testunsigned, state)
            .fatal("formating test queue");
        auto q(server->openqueue(proto::eq::names::testunsigned, state)
               .fatal("opening test queue"));
        waitbox<void> startedwaiter;
        tests::hook<void> h(geneqclient::startingwaiter,
                            [&startedwaiter] {
                                startedwaiter.set(); });
        auto cconfig(eqclientconfig::dflt());
        cconfig.get = timedelta::milliseconds(500);
        cconfig.wait = timedelta::milliseconds(100);
        auto c(eqclient<unsigned>::connect(
                   io,
                   *pool,
                   sn,
                   proto::eq::names::testunsigned,
                   timedelta::seconds(3).future(),
                   cconfig)
               .fatal("connecting eqclient")
               .first());
        startedwaiter.get(io);
        c->destroy();
        pool->destroy();
        s->destroy(io);
        q->destroy(io);
        server->destroy();
        state.unlink().fatal("unlinking test queue"); },
    "dropnosub", [] (clientio io) {
        quickcheck qc;
        auto cn(mkrandom<clustername>(qc));
        agentname sn(qc);
        auto pool(connpool::build(cn).fatal("starting conn pool"));
        auto server(eqserver::build());
        auto s(rpcservice2::listen<eqtestserver>(
                   io,
                   cn,
                   sn,
                   peername::all(peername::port::any),
                   *server)
               .fatal("starting service"));
        filename state("S");
        state.unlink();
        eqserver::formatqueue(proto::eq::names::testunsigned, state)
            .fatal("formating test queue");
        auto q(server->openqueue(proto::eq::names::testunsigned, state)
               .fatal("opening test queue"));
        q->queue(99, rpcservice2::acquirestxlock(io));
        auto c(eqclient<unsigned>::connect(
                   io,
                   *pool,
                   sn,
                   proto::eq::names::testunsigned,
                   timedelta::seconds(3).future())
               .fatal("connecting eqclient")
               .first());
        assert(c->pop() == Nothing);
        q->queue(3, rpcservice2::acquirestxlock(io));
        assert(c->pop(io) == 3);
        c->destroy();
        pool->destroy();
        s->destroy(io);
        server->destroy();
        q->destroy(io);
        state.unlink().fatal("unlinking test queue"); },
    "multiqueue", [] (clientio io) {
        quickcheck qc;
        auto cn(mkrandom<clustername>(qc));
        agentname sn(qc);
        auto pool(connpool::build(cn).fatal("starting conn pool"));
        auto server(eqserver::build());
        auto s(rpcservice2::listen<eqtestserver>(
                   io,
                   cn,
                   sn,
                   peername::all(peername::port::any),
                   *server)
               .fatal("starting service"));
        filename state1("S");
        filename state2("T");
        state1.unlink();
        state2.unlink();
        eqserver::formatqueue(proto::eq::names::testunsigned, state1)
            .fatal("format queue 1");
        eqserver::formatqueue(proto::eq::names::teststring, state2)
            .fatal("format queue 2");
        auto q1(server->openqueue(proto::eq::names::testunsigned, state1)
                .fatal("open queue 1"));
        auto q2(server->openqueue(proto::eq::names::teststring, state2)
                .fatal("open queue 2"));
        auto c1(eqclient<unsigned>::connect(
                    io,
                    *pool,
                    sn,
                    proto::eq::names::testunsigned,
                    timedelta::seconds(3).future())
                .fatal("connecting eqclient")
                .first());
        auto c2(eqclient<string>::connect(
                    io,
                    *pool,
                    sn,
                    proto::eq::names::teststring,
                    timedelta::seconds(3).future())
                .fatal("connecting eqclient")
                .first());
        q1->queue(99, rpcservice2::acquirestxlock(io));
        q2->queue("Hello", rpcservice2::acquirestxlock(io));
        tassert(T(c1->pop(io)) == T(99u));
        tassert(T(c2->pop(io)) == T(string("Hello")));
        c1->destroy();
        q2->destroy(io);
        s->destroy(io);
        server->destroy();
        q1->destroy(io);
        c2->destroy();
        state1.unlink();
        state2.unlink();
        pool->destroy(); },
    "pipeline", [] (clientio io) {
        quickcheck qc;
        auto cn(mkrandom<clustername>(qc));
        agentname sn(qc);
        auto pool(connpool::build(cn).fatal("starting conn pool"));
        auto server(eqserver::build());
        auto s(rpcservice2::listen<eqtestserver>(
                   io,
                   cn,
                   sn,
                   peername::all(peername::port::any),
                   *server)
               .fatal("starting service"));
        filename state("S");
        state.unlink();
        eqserver::formatqueue(proto::eq::names::testunsigned, state)
            .fatal("formating test queue");
        auto q(server->openqueue(proto::eq::names::testunsigned, state)
               .fatal("opening test queue"));
        auto cconfig(eqclientconfig::dflt());
        cconfig.maxqueue = 2;
        auto c(eqclient<unsigned>::connect(
                   io,
                   *pool,
                   sn,
                   proto::eq::names::testunsigned,
                   timedelta::seconds(3).future(),
                   cconfig)
               .fatal("connecting eqclient")
               .first());
        q->queue(0, rpcservice2::acquirestxlock(io));
        for (unsigned x = 0; x < 1000; x++) {
            q->queue(x+1, rpcservice2::acquirestxlock(io));
            assert(c->pop(io) == x); }
        c->destroy();
        s->destroy(io);
        q->destroy(io);
        server->destroy();
        pool->destroy();
        state.unlink().fatal("unlinking test queue"); },
    "multiclient2", [] (clientio io) {
        /* Slightly more interesting variant of abandoning a
         * wait. */
        quickcheck qc;
        auto qconf(eventqueueconfig::dflt());
        qconf.queuelimit = 2;
        auto cn(mkrandom<clustername>(qc));
        agentname sn(qc);
        auto pool(connpool::build(cn)
                  .fatal("starting conn pool"));
        auto server(eqserver::build());
        auto s(rpcservice2::listen<eqtestserver>(
                   io,
                   cn,
                   sn,
                   peername::all(peername::port::any),
                   *server)
               .fatal("starting service"));
        filename state("S");
        state.unlink();
        eqserver::formatqueue(proto::eq::names::testunsigned, state, qconf)
            .fatal("formating test queue");
        auto q(server->openqueue(proto::eq::names::testunsigned, state)
               .fatal("opening test queue"));
        unsigned nrwaiters = 0;
        tests::hook<void> h(geneqclient::startingwaiter, [&] { nrwaiters++; });
        auto c1(eqclient<unsigned>::connect(
                    io,
                    *pool,
                    sn,
                    proto::eq::names::testunsigned,
                    timedelta::seconds(3).future())
                .fatal("connecting eqclient")
                .first());
        while (nrwaiters == 0) timedelta::milliseconds(1).future().sleep(io);
        assert(nrwaiters == 1);
        auto c2(eqclient<unsigned>::connect(
                    io,
                    *pool,
                    sn,
                    proto::eq::names::testunsigned,
                    timedelta::seconds(3).future())
                .fatal("connecting eqclient")
                .first());
        while (nrwaiters == 1) timedelta::milliseconds(1).future().sleep(io);
        assert(nrwaiters == 2);
        c2->destroy();
        /* Ick */
        timedelta::milliseconds(200).future().sleep(io);
        c1->destroy();
        s->destroy(io);
        q->destroy(io);
        server->destroy();
        pool->destroy();
        state.unlink().fatal("unlinking test queue"); },
    "asyncconnect", [] (clientio io) {
        quickcheck qc;
        auto cn(mkrandom<clustername>(qc));
        agentname sn(qc);
        auto pool(connpool::build(cn)
                  .fatal("starting conn pool"));
        auto server(eqserver::build());
        waitbox<void> servercaptured;
        waitbox<void> serverrelease;
        auto s(rpcservice2::listen<eqslowtestserver>(
                   io,
                   cn,
                   sn,
                   peername::all(peername::port::any),
                   *server,
                   servercaptured,
                   serverrelease)
               .fatal("starting service"));
        filename state("S");
        state.unlink();
        eqserver::formatqueue(proto::eq::names::testunsigned, state)
            .fatal("formating test queue");
        auto q(server->openqueue(proto::eq::names::testunsigned, state)
               .fatal("opening test queue"));
        auto start(timestamp::now());
        auto conn(eqclient<unsigned>::connect(
                      *pool,
                      sn,
                      proto::eq::names::testunsigned));
        tassert(T(timestamp::now()) - T(start) < T(300_ms * TIMEDILATE));
        servercaptured.get(io);
        tassert(T(timestamp::now()) - T(start) < T(300_ms * TIMEDILATE));
        assert(conn->finished() == Nothing);
        {   subscriber sub;
            subscription ss(sub, conn->pub());
            assert(sub.poll() == &ss);
            assert(sub.poll() == NULL);
            serverrelease.set();
            assert(timedelta::time([&] { assert(sub.wait(io) == &ss);  })
                   < timedelta::milliseconds(100)); }
        auto c(conn->pop(conn->finished().just())
               .fatal("connecting to slow server")
               .first());
        /* Quick check that it vaguely works. */
        q->queue(52, io);
        assert(timedelta::time([&] { assert(c->pop(io) == 52); })
               < timedelta::milliseconds(500));
        c->destroy();
        s->destroy(io);
        server->destroy();
        pool->destroy();
        q->destroy(io);
        state.unlink().fatal("unlinking test queue"); },
    "evadvance", [] (clientio io) {
        quickcheck qc;
        auto cn(mkrandom<clustername>(qc));
        agentname sn(qc);
        auto pool(connpool::build(cn).fatal("starting conn pool"));
        auto server(eqserver::build());
        auto s(rpcservice2::listen<eqtestserver>(
                   io,
                   cn,
                   sn,
                   peername::all(peername::port::any),
                   *server)
               .fatal("starting service"));
        filename statefile("S");
        statefile.unlink();
        eqserver::formatqueue(proto::eq::names::testunsigned, statefile)
            .fatal("formating test queue");
        auto q(server->openqueue(proto::eq::names::testunsigned, statefile)
               .fatal("opening test queue"));
        auto droppedeid(q->queue(123, io));
        auto cpair(eqclient<unsigned>::connect(
                       io,
                       *pool,
                       sn,
                       proto::eq::names::testunsigned,
                       timedelta::seconds(3).future())
                   .fatal("connecting eqclient"));
        assert(droppedeid <= cpair.second());
        auto c(cpair.first());
        auto exptag(q->lastid());
        auto tag1(q->queue(5, io));
        assert(exptag.succ() == tag1);
        assert(q->lastid() == tag1);
        assert(tag1 > cpair.second());
        auto pop1res(c->popid(io).fatal("popping first queue entry"));
        assert(pop1res.first() == tag1);
        assert(pop1res.second() == 5);
        auto tag2(q->queue(6, io));
        assert(tag2 > tag1);
        auto pop2res(c->popid(io).fatal("popping second queue entry"));
        assert(pop2res.first() == tag2);
        assert(pop2res.second() == 6);

        q->destroy(io);
        q = server->openqueue(proto::eq::names::testunsigned, statefile)
            .fatal("re-opening test queue");

        /* Server restarts always carry a risk of events being
         * dropped, so for safety clients should all report that
         * events were potentially dropped. They can also report
         * badqueue if they happened to look at the queue in the
         * interval when it wasn't registered. */
        auto rr(c->pop(io));
        tassert(T(rr) == T(error::eventsdropped) ||
                T(rr) == T(error::badqueue));
        c->destroy();
        c = eqclient<unsigned>::connect(
            io,
            *pool,
            sn,
            proto::eq::names::testunsigned,
            timedelta::seconds(3).future())
            .fatal("reconnecting eqclient")
            .first();

        auto tag3(q->queue(7, io));
        assert(tag3 > tag2);
        auto pop3res(c->popid(io).fatal("popping third queue entry"));
        assert(pop3res.first() == tag3);
        assert(pop3res.second() == 7);

        c->destroy();
        s->destroy(io);
        server->destroy();
        pool->destroy();
        q->destroy(io);
        statefile.unlink().fatal("unlinking test queue"); },
    "abortconnect", [] (clientio) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto pool(connpool::build(cn)
                  .fatal("starting conn pool"));
        eqclient<unsigned>::connect(
            *pool,
            sn,
            proto::eq::names::testunsigned)
            ->abort();
        pool->destroy(); },
    "eidparser", [] {
        parsers::roundtrip(proto::eq::eventid::_parser()); },
    "badconnect", [] (clientio io) {
        quickcheck q;
        agentname an(q);
        auto cn(mkrandom<clustername>(q));
        auto &dummy(*rpcservice2::listen<dummyservice>(
                        io,
                        cn,
                        an,
                        peername::all(peername::port::any))
                    .fatal("starting dummy service"));
        auto &pool(*connpool::build(cn)
                   .fatal("starting conn pool"));
        assert(eqclient<unsigned>::connect(
                   io,
                   pool,
                   an,
                   proto::eq::names::testunsigned,
                   timestamp::now() + timedelta::seconds(3))
               == error::dlopen);
        pool.destroy();
        dummy.destroy(io); });
