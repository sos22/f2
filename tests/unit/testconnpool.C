/* This also acts as a test for rpcservice2. */
#include "connpool.H"
#include "rpcservice2.H"
#include "socket.H"
#include "spark.H"
#include "spawn.H"
#include "test.H"
#include "testassert.H"
#include "test2.H"

#include "tests/lib/testservices.H"

#include "connpool.tmpl"
#include "list.tmpl"
#include "maybe.tmpl"
#include "orerror.tmpl"
#include "rpcservice2.tmpl"
#include "spark.tmpl"
#include "test.tmpl"
#include "testassert.tmpl"
#include "test2.tmpl"

#include "tests/lib/testservices.tmpl"

class echoservice : public rpcservice2 {
private: unsigned cntr;
public:  echoservice(const rpcservice2::constoken &t)
    : rpcservice2(t, interfacetype::test),
      cntr(73) {}
public: orerror<void> called(
    clientio io,
    deserialise1 &ds,
    interfacetype,
    nnp<incompletecall> ic,
    onconnectionthread oct) final {
    string msg(ds);
    if (ds.isfailure()) return ds.failure();
    if (cntr == 75) {
        cntr++;
        ic->fail(error::toolate, io, oct); }
    else {
        ic->complete(
            [&msg, this] (serialise1 &s,
                          mutex_t::token,
                          onconnectionthread) {
                msg.serialise(s);
                s.push(cntr++); },
            io,
            oct); }
    return Success; } };

class pingservice : public rpcservice2 {
public:  explicit pingservice(const rpcservice2::constoken &t)
    : rpcservice2(t, interfacetype::test) {}
public: orerror<void> called(
    clientio io,
    deserialise1 &,
    interfacetype,
    nnp<incompletecall> ic,
    onconnectionthread oct) final {
    ic->complete(
        [] (serialise1 &, mutex_t::token, onconnectionthread) {}, io, oct);
    return Success; } };

class abandonservice : public rpcservice2 {
public: maybe<spark<void> > worker;
public: waitbox<void> &abandoned;
public: abandonservice(const rpcservice2::constoken &t,
                       waitbox<void> &_abandoned)
    : rpcservice2(t, interfacetype::test),
      worker(Nothing),
      abandoned(_abandoned) {}
public: orerror<void> called(
    clientio,
    deserialise1 &,
    interfacetype,
    nnp<incompletecall> ic,
    onconnectionthread) final {
    worker.mkjust([this, ic] () {
            ic->abandoned().get(clientio::CLIENTIO);
            abandoned.set();
            ic->fail(error::toolate, acquirestxlock(clientio::CLIENTIO)); });
    return Success; } };

class raceservice : public rpcservice2 {
public: list<spark<void> > outstanding;
public: bool paused;
public: raceservice(const rpcservice2::constoken &t)
    : rpcservice2(t, interfacetype::test),
      paused(false) {}
public: orerror<void> called(
    clientio,
    deserialise1 &ds,
    interfacetype,
    nnp<incompletecall> ic,
    onconnectionthread) final {
    timedelta delay(ds);
    if (ds.isfailure()) return ds.failure();
    auto deadline(delay.future());
    outstanding.append([this, deadline, ic] {
            deadline.sleep(clientio::CLIENTIO);
            assert(!paused);
            ic->complete(
                [] (serialise1 &, mutex_t::token) { },
                acquirestxlock(clientio::CLIENTIO)); });
    return Success; } };

class largerespservice : public rpcservice2 {
public: string largestring;
public: largerespservice(const constoken &t)
    : rpcservice2(t, interfacetype::test),
      largestring("Hello world, this is a test pattern") {
    /* Make it about eight megs. */
    for (unsigned x = 0; x < 18; x++) largestring = largestring + largestring; }
public: orerror<void> called(
    clientio io,
    deserialise1 &,
    interfacetype,
    nnp<incompletecall> ic,
    onconnectionthread oct) final {
    ic->complete(
        [this] (serialise1 &s, mutex_t::token, onconnectionthread) {
            largestring.serialise(s); },
        acquirestxlock(io),
        oct);
    return Success; } };

class largereqservice : public rpcservice2 {
public: string largestring;
public: largereqservice(const constoken &t)
    : rpcservice2(t, interfacetype::test),
      largestring("Hello world, this is another test pattern") {
    for (unsigned x = 0; x < 18; x++) largestring = largestring + largestring; }
public: orerror<void> called(
    clientio io,
    deserialise1 &ds,
    interfacetype,
    nnp<incompletecall> ic,
    onconnectionthread oct) final {
    string s(ds);
    assert(s == largestring);
    /* Slow down thread to make it a bit easier for the client to fill their
     * TX buffer. */
    (10_ms).future().sleep(io);
    ic->complete(
        [this] (serialise1 &, mutex_t::token, onconnectionthread) {},
        acquirestxlock(io),
        oct);
    return Success; } };

class bufferservice : public rpcservice2 {
public: bufferservice(const constoken &t) : rpcservice2(t,interfacetype::test){}
public: orerror<void> called(
    clientio io,
    deserialise1 &ds,
    interfacetype,
    nnp<incompletecall> ic,
    onconnectionthread oct) final {
    ::buffer b(ds);
    assert(!memcmp(b.linearise(0, 5), "HELLO", 5));
    ic->complete(
        [] (serialise1 &s, mutex_t::token /* txlock */, onconnectionthread) {
            ::buffer bb;
            bb.queue("GOODBYE", 7);
            bb.serialise(s); },
        acquirestxlock(io),
        oct);
    return Success; } };

class abortservice : public rpcservice2 {
public: waitbox<void> &callstarted;
public: waitbox<void> &callaborted;
public: maybe<spark<void> > worker;
public: abortservice(const constoken &t,
                     waitbox<void> &_callstarted,
                     waitbox<void> &_callaborted)
    : rpcservice2(t, interfacetype::test),
      callstarted(_callstarted),
      callaborted(_callaborted),
      worker(Nothing) {}
public: orerror<void> called(
    clientio,
    deserialise1 &,
    interfacetype,
    nnp<incompletecall> ic,
    onconnectionthread) final {
    callstarted.set();
    assert(worker == Nothing);
    worker.mkjust([this, ic] {
            {   subscriber sub;
                subscription ss(sub, ic->abandoned().pub());
                while (!ic->abandoned().ready()) sub.wait(clientio::CLIENTIO); }
            callaborted.set();
            ic->fail(error::toosoon, acquirestxlock(clientio::CLIENTIO)); });
    return Success; } };

class failinitservice : public rpcservice2 {
public: explicit failinitservice(const constoken &t)
    : rpcservice2(t, interfacetype::test) {}
public: orerror<void> initialise(clientio) { return error::toolate; }
public: orerror<void> called(clientio,
                             deserialise1 &,
                             interfacetype,
                             nnp<rpcservice2::incompletecall>,
                             rpcservice2::onconnectionthread) {
    abort(); } };

class incompletecallfield : public rpcservice2 {
public: list<string> msgs;
public: explicit incompletecallfield(const constoken &t)
    : rpcservice2(t, interfacetype::test) {}
public: orerror<void> called(
    clientio,
    deserialise1 &,
    interfacetype,
    nnp<incompletecall> ic,
    onconnectionthread) final {
    msgs.append(ic->field().c_str());
    return error::toosoon; } };

static testmodule __testconnpool(
    "connpool",
    list<filename>::mk("connpool.C",
                       "connpool.H",
                       "connpool.tmpl",
                       "rpcservice2.C",
                       "rpcservice2.H",
                       "rpcservice2.tmpl"),
    testmodule::LineCoverage(85_pc),
    testmodule::BranchCoverage(69_pc),
    "null", [] (clientio io) {
        /* Tests of what happens when there's nothing to connect
         * to. */
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        auto start(timestamp::now());
        auto pool(connpool::build(cn).fatal("starting conn pool"));
        tassert(T(timestamp::now()) < T(start) + T(100_ms));
        start = timestamp::now();
        assert(pool->call<void>(
                   io,
                   agentname("nonesuch"),
                   interfacetype::test,
                   timestamp::now() + timedelta::milliseconds(10),
                   [] (serialise1 &, connpool::connlock) {
                       /* Shouldn't connect enough to do a
                        * serialise. */
                       abort(); },
                   []
                   (orerror<nnp<deserialise1> > e, connpool::connlock)
                   -> orerror<void> {
                       assert(e == error::timeout);
                       return error::dlopen; })
               == error::dlopen);
        auto end(timestamp::now());
        tassert(T(end) > T(start) + T(10_ms));
        tassert(T(end) < T(start) + T(110_ms));
        start = timestamp::now();
        auto c(pool->call<void>(
                   agentname("nonesuch"),
                   interfacetype::test,
                   timestamp::now() + timedelta::hours(10),
                   [] (serialise1 &, connpool::connlock) { abort(); },
                   []
                   (orerror<nnp<deserialise1> > e, connpool::connlock)
                   -> orerror<void> {
                       assert(e == error::aborted);
                       return error::dlopen; }));
        end = timestamp::now();
        /* Starting the call should be very cheap. */
        tassert(T(end) < T(start) + T(10_ms));
        start = end;
        assert(c->abort() == error::dlopen);
        end = timestamp::now();
        /* Aborting can be a little more expensive, but not
         * much. */
        assert(end < start + timedelta::milliseconds(50));
        c = pool->call<void>(
            agentname("nonesuch"),
            interfacetype::test,
            timestamp::now() + timedelta::hours(10),
            [] (serialise1 &, connpool::connlock) { abort(); },
            []
            (orerror<nnp<deserialise1> > e, connpool::connlock)
            -> orerror<void> {
                assert(e == error::disconnected);
                return error::dlopen; });
        start = timestamp::now();
        pool->destroy();
        end = timestamp::now();
        tassert(T(end) - T(start) < T(timedelta::milliseconds(50)));
        start = end;
        assert(c->pop(io) == error::dlopen);
        end = timestamp::now();
        tassert(T(end) - T(start) < T(timedelta::milliseconds(10))); },
    "getconfig", [] (clientio) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        auto pool(connpool::build(cn).fatal("starting conn pool"));
        assert(pool->getconfig().beacon.cluster() == cn);
        pool->destroy(); },
    "echo", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto srv(rpcservice2::listen<echoservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting echo service"));
        auto pool(connpool::build(cn).fatal("starting conn pool"));
        pool->call<void>(
            io,
            sn,
            interfacetype::test,
            timestamp::now() + timedelta::hours(1),
            [] (serialise1 &s, connpool::connlock) {
                string("HELLO!").serialise(s); },
            []
            (deserialise1 &ds, connpool::connlock) -> orerror<void> {
                string msg(ds);
                unsigned cntr(ds);
                assert(!ds.isfailure());
                assert(msg == "HELLO!");
                assert(cntr == 73);
                return Success; })
            .fatal("calling echo service");
        assert(
            pool->call<int>(
                io,
                sn,
                interfacetype::test,
                timestamp::now() + timedelta::hours(1),
                [] (serialise1 &s, connpool::connlock) {
                    string("GOODBYE!").serialise(s); },
                []
                (deserialise1 &ds, connpool::connlock) -> orerror<int> {
                    string msg(ds);
                    unsigned cntr(ds);
                    assert(!ds.isfailure());
                    assert(msg == "GOODBYE!");
                    assert(cntr == 74);
                    return 9; })
            == 9);
        auto r = pool->call<char *>(
            io,
            sn,
            interfacetype::test,
            timestamp::now() + timedelta::hours(1),
            [] (serialise1 &s, connpool::connlock) {
                string("GOODBYE!").serialise(s); },
            [] (deserialise1 &, connpool::connlock) -> orerror<char *> {
                abort(); } );
        assert(r == error::toolate);
        assert(
            pool->call<char *>(
                io,
                sn,
                interfacetype::test,
                timestamp::now() + timedelta::hours(1),
                [] (serialise1 &s, connpool::connlock) {
                    string("boo").serialise(s); },
                [] (deserialise1 &ds, connpool::connlock)
                -> orerror<char *>{
                    string msg(ds);
                    unsigned cntr(ds);
                    assert(!ds.isfailure());
                    assert(msg == "boo");
                    assert(cntr == 76);
                    return (char *)7; })
            == (char *)7);
        pool->destroy();
        srv->destroy(io); },
    "abandon1", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        waitbox<void> abandoned;
        auto srv(rpcservice2::listen<abandonservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any),
                     abandoned)
                 .fatal("starting abandon service"));
        auto pool(connpool::build(cn).fatal("building connpool"));
        auto call(pool->call<void>(
                      sn,
                      interfacetype::test,
                      timestamp::now() + timedelta::hours(1),
                      [] (serialise1 &, connpool::connlock) {},
                      [] (connpool::asynccall &,
                          orerror<nnp<deserialise1> > d,
                          connpool::connlock) {
                          assert(d == error::disconnected);
                          return error::toosoon; }));
        (timedelta::milliseconds(100).future()).sleep(io);
        assert(call->finished() == Nothing);
        pool->destroy();
        assert(call->pop(io) == error::toosoon);
        abandoned.get(io);
        srv->destroy(io); },
    "timeoutcall", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        waitbox<void> abandoned;
        auto srv(rpcservice2::listen<abandonservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any),
                     abandoned)
                 .fatal("starting abandon service"));
        auto pool(connpool::build(cn).fatal("building connpool"));
        assert(pool->call<void>(
                   io,
                   sn,
                   interfacetype::test,
                   timestamp::now() + timedelta::milliseconds(100),
                   [] (serialise1 &, connpool::connlock) {},
                   [] (deserialise1 &, connpool::connlock)
                   -> orerror<void> {
                       abort(); })
               == error::timeout);
        (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
        assert(!abandoned.ready());
        srv->destroy(io);
        abandoned.get(io);
        pool->destroy(); },
    "slow", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto srv(rpcservice2::listen<slowservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting slow service"));
        auto pool(connpool::build(cn).fatal("building connpool"));
        maybe<timestamp> completed1(Nothing);
        maybe<timestamp> completed2(Nothing);
        list<nnp<connpool::asynccall> > completed;
        auto call1(pool->call<void>(
                       sn,
                       interfacetype::test,
                       timestamp::now() + timedelta::hours(1),
                       [] (serialise1 &s, connpool::connlock) {
                           timedelta::milliseconds(200).serialise(s);
                           s.push((unsigned)1); },
                       [&completed, &completed1]
                       (connpool::asynccall &ac,
                        orerror<nnp<deserialise1> > d,
                        connpool::connlock)
                       -> orerror<void> {
                           d.fatal("getting response from slow service");
                           unsigned k(*d.success());
                           assert(k == 1);
                           assert(completed1 == Nothing);
                           completed1 = timestamp::now();
                           completed.pushtail(ac);
                           return Success; } ) );
        auto call2(pool->call<void>(
                       sn,
                       interfacetype::test,
                       timestamp::now() + timedelta::hours(1),
                       [] (serialise1 &s, connpool::connlock) {
                           timedelta::milliseconds(100).serialise(s);
                           s.push((unsigned)2); },
                       [&completed, &completed2]
                       (connpool::asynccall &ac,
                        orerror<nnp<deserialise1> > d,
                        connpool::connlock)
                       -> orerror<void> {
                           d.fatal("getting response from slow service");
                           unsigned k(*d.success());
                           assert(k == 2);
                           assert(completed2 == Nothing);
                           completed2 = timestamp::now();
                           completed.pushtail(ac);
                           return Success; } ) );
        assert(call1->pop(io) == Success);
        assert(completed1.isjust());
        assert(completed2.isjust());
        assert(completed1.just() > completed2.just());
        assert(call2->finished().isjust());
        assert(call2->pop(io) == Success);
        assert(completed.pophead() == call2);
        assert(completed.pophead() == call1);
        pool->destroy();
        srv->destroy(io); },
    "slowabandon", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto srv(rpcservice2::listen<slowservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting slow service"));
        auto pool(connpool::build(cn).fatal("building connpool"));
        auto call(pool->call<void>(
                      sn,
                      interfacetype::test,
                      timestamp::now() + timedelta::hours(1),
                      [] (serialise1 &s, connpool::connlock) {
                          timedelta::hours(3).serialise(s);
                          s.push((unsigned)3); },
                      [] (connpool::asynccall &,
                          orerror<nnp<deserialise1> > d,
                          connpool::connlock) -> orerror<void> {
                          assert(d == error::aborted);
                          return Success; }) );
        /* Wait for the call to start. */
        while (srv->outstanding.empty()) {
            (timestamp::now() + timedelta::milliseconds(1)).sleep(io); }
        /* Shut down server while it's got outstanding calls.
         * Should be able to do it quickly. */
        tassert(T2(timedelta,
                   timedelta::time([srv, io] { srv->destroy(io); })) <
                T(100_ms));
        tassert(T2(timedelta, timedelta::time([call, io] { call->abort(); })) <
                T(100_ms));
        tassert(T2(timedelta, timedelta::time([pool] { pool->destroy(); })) <
                T(100_ms)); },
    "abortcompleted", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto srv(rpcservice2::listen<echoservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting echo service"));
        auto pool(connpool::build(cn).fatal("building connpool"));
        auto call(pool->call<int>(
                      sn,
                      interfacetype::test,
                      timestamp::now() + timedelta::hours(1),
                      [] (serialise1 &s, connpool::connlock) {
                          string("foo").serialise(s); },
                      [] (connpool::asynccallT<int> &,
                          orerror<nnp<deserialise1> > d,
                          connpool::connlock) -> orerror<int> {
                          assert(string(*d.success()) == "foo");
                          return 1023; }));
        while (call->finished() == Nothing) (1_ms).future().sleep(io);
        ::logmsg(loglevel::info, "aborting");
        assert(call->abort() == 1023);
        ::logmsg(loglevel::info, "aborted");
        pool->destroy();
        srv->destroy(io); },
    "clientdisco", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        waitbox<void> died;
        tests::hook<void> h(rpcservice2::clientdisconnected,
                            [&died] { if (!died.ready()) died.set(); });
        auto srv(rpcservice2::listen<echoservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting echo service"));
        auto pool1(connpool::build(cn).fatal("building pool1"));
        /* Run a call to force the pool to connect */
        assert(pool1->call<int>(
                   io,
                   sn,
                   interfacetype::test,
                   timestamp::now() + timedelta::hours(1),
                   [] (serialise1 &s, connpool::connlock) {
                       string("").serialise(s); },
                   [] (deserialise1 &ds, connpool::connlock) ->
                   orerror<int> {
                       string msg(ds);
                       unsigned cntr(ds);
                       assert(!ds.isfailure());
                       assert(msg == "");
                       assert(cntr == 73);
                       return 9; })
               == 9);
        auto pool2(connpool::build(cn).fatal("building pool2"));
        /* Force pool2 to connect as well. */
        assert(pool2->call<void>(
                   io,
                   sn,
                   interfacetype::test,
                   timestamp::now() + timedelta::hours(1),
                   [] (serialise1 &s, connpool::connlock) {
                       string("").serialise(s); },
                   [] (deserialise1 &ds, connpool::connlock) ->
                   orerror<void> {
                       string msg(ds);
                       unsigned cntr(ds);
                       assert(!ds.isfailure());
                       assert(msg == "");
                       assert(cntr == 74);
                       return Success; })
               == Success);
        pool2->destroy();
        tassert(T2(timedelta, timedelta::time([&died, io] { died.get(io); }))
                < T(timedelta::milliseconds(100)));
        pool1->destroy();
        srv->destroy(io); },
    "largeresp", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto config(rpcservice2config::dflt(cn, sn));
        /* Use a small TX buffer limit to make things a bit
         * easier. */
        config.txbufferlimit = 100;
        /* Bump up call limit a bit so that we don't hit that
         * first. */
        config.maxoutstandingcalls = 1000000;
        auto srv(rpcservice2::listen<largerespservice>(
                     io,
                     config,
                     peername::all(peername::port::any))
                 .fatal("starting large response service"));
        auto pool(connpool::build(cn).fatal("building pool1"));
        list<nnp<connpool::asynccall> > outstanding;
        for (unsigned x = 0; x < 10; x++) {
            outstanding.pushtail(
                pool->call<void>(
                    sn,
                    interfacetype::test,
                    timestamp::now() + timedelta::hours(1),
                    [] (serialise1 &, connpool::connlock) {},
                    [srv, io] (connpool::asynccall &,
                               orerror<nnp<deserialise1> > d,
                               connpool::connlock)
                    -> orerror<void> {
                        assert(d.issuccess());
                        string s(*d.success());
                        assert(s == srv->largestring);
                        /* Deliberately slow the client conn
                         * thread down a bit to make it easier for
                         * the service to fill the buffer. */
                        (timestamp::now() + timedelta::milliseconds(1))
                            .sleep(io);
                        return Success; })); }
        while (!outstanding.empty()) {
            assert(outstanding.pophead()->pop(io) == Success); }
        pool->destroy();
        srv->destroy(io); },
    "largereq", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto srv(rpcservice2::listen<largereqservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting large request service"));
        auto pool(connpool::build(cn).fatal("building pool"));
        list<nnp<connpool::asynccall> > outstanding;
        for (unsigned x = 0; x < 10; x++) {
            outstanding.pushtail(
                pool->call<void>(
                    sn,
                    interfacetype::test,
                    timestamp::now() + timedelta::hours(1),
                    [srv] (serialise1 &s, connpool::connlock) {
                        srv->largestring.serialise(s); },
                    [srv, io] (connpool::asynccall &,
                               orerror<nnp<deserialise1> > d,
                               connpool::connlock)
                    -> orerror<void> {
                        assert(d.issuccess());
                        return Success; })); }
        while (!outstanding.empty()) {
            assert(outstanding.pophead()->pop(io) == Success); }
        pool->destroy();
        srv->destroy(io); },
    "returnbuffer", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto srv(rpcservice2::listen<bufferservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting buffer service"));
        auto pool(connpool::build(cn).fatal("building pool"));
        auto b(pool->call< ::buffer >(
                   io,
                   sn,
                   interfacetype::test,
                   timestamp::now() + timedelta::hours(1),
                   [] (serialise1 &s, connpool::connlock) {
                       ::buffer buf;
                       buf.queue("HELLO", 5);
                       buf.serialise(s); },
                   [] (deserialise1 &ds, connpool::connlock)
                   -> orerror< ::buffer> {
                       return (::buffer)ds; })
               .fatal("calling buffer service"));
        assert(!memcmp(b.linearise(0, 7), "GOODBYE", 7));
        pool->destroy();
        srv->destroy(io); },
    "slowcall", [] (clientio io) {
        /* The conn pool shouldn't drop connections which still
         * have outstanding calls. */
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto srv(rpcservice2::listen<slowservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting slow service"));
        auto config(connpool::config::dflt(cn));
        config.idletimeout = timedelta::milliseconds(50);
        auto pool(connpool::build(config).fatal("building connpool"));
        maybe<timestamp> finished(Nothing);
        auto start(timestamp::now());
        pool->call<void>(
            io,
            sn,
            interfacetype::test,
            timestamp::now() + timedelta::hours(1),
            [] (serialise1 &s, connpool::connlock) {
                timedelta::milliseconds(200).serialise(s);
                s.push((unsigned)12345678); },
            [&finished]
            (orerror<nnp<deserialise1> > d, connpool::connlock)
            -> orerror<void> {
                d.fatal("getting response from slow service");
                unsigned k(*d.success());
                assert(k == 12345678);
                assert(finished == Nothing);
                finished = timestamp::now();
                return Success; })
            .fatal("doing call");
        auto end(timestamp::now());
        /* Must have made it to the target time, even though the
         * idle timeout on the pool is only 50ms. */
        tassert(T2(timestamp, finished.just())-T(start) > T(200_ms));
        /* But not too far past it. */
        tassert(T(end) - T(finished.just()) < T(timedelta::milliseconds(100)));
        tassert(T(end) - T(start) < T(timedelta::milliseconds(500)));
        pool->destroy();
        srv->destroy(io); },
    "abort1", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        waitbox<void> callstarted;
        waitbox<void> callaborted;
        auto srv(rpcservice2::listen<abortservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any),
                     callstarted,
                     callaborted)
                 .fatal("starting abort service"));
        auto pool(connpool::build(cn).fatal("building pool"));
        auto b(pool->call<void>(
                   sn,
                   interfacetype::test,
                   timedelta::hours(1).future(),
                   [] (serialise1 &, connpool::connlock) {},
                   [] (deserialise1 &, connpool::connlock)
                   -> orerror<void> {
                       abort(); }));
        callstarted.get(io);
        assert(b->abort() == error::aborted);
        tassert(T2(timedelta,
                   timedelta::time([&callaborted, io] {
                           callaborted.get(io); }))
                < T(timedelta::milliseconds(50)));
        pool->destroy();
        srv->destroy(io); },
    "abort2", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        waitbox<void> callstarted;
        waitbox<void> callaborted;
        auto srv(rpcservice2::listen<pingservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting ping service"));
        auto pool(connpool::build(cn).fatal("building pool"));
        for (unsigned x = 0; x < 100; x++) {
            pool->call(sn,
                       interfacetype::test,
                       timedelta::hours(1).future(),
                       [] (serialise1 &, connpool::connlock) {})
                ->abort(); }
        for (unsigned x = 0; x < 100; x++) {
            auto c1(pool->call(sn,
                               interfacetype::test,
                               timedelta::hours(1).future(),
                               [] (serialise1 &, connpool::connlock) {}));
            pool->call(sn,
                       interfacetype::test,
                       timedelta::hours(1).future(),
                       [] (serialise1 &, connpool::connlock) {})
                ->abort();
            c1->abort(); }
        pool->destroy();
        srv->destroy(io); },
    "config", [] {
        quickcheck q;
        beaconclientconfig bcc(mkrandom<clustername>(q));
        assert(connpool::config::mk(bcc,
                                    timedelta::seconds(-1)) ==
               error::invalidparameter);
        assert(connpool::config(bcc) == connpool::config::mk(bcc)); },
    "connreap", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn1(q);
        agentname sn2(q);
        while (sn2 == sn1) sn2 = q;
        ::logmsg(loglevel::debug, "agent name 1 " + fields::mk(sn1));
        ::logmsg(loglevel::debug, "agent name 2 " + fields::mk(sn2));
        auto srv1(rpcservice2::listen<echoservice>(
                      io,
                      cn,
                      sn1,
                      peername::all(peername::port::any))
                  .fatal("starting echo service"));
        auto srv2(rpcservice2::listen<echoservice>(
                      io,
                      cn,
                      sn2,
                      peername::all(peername::port::any))
                  .fatal("starting echo service"));
        auto cconfig(connpool::config::dflt(cn));
        cconfig.idletimeout = timedelta::milliseconds(50);
        auto pool(connpool::build(cconfig).fatal("building connpool"));
        unsigned nrreaped(0);
        tests::hook<void> h(connpool::reapedconnthread,
                            [&nrreaped] { nrreaped++; });
        /* Not really looking at the results of the calls, but the
         * order we do them in matters to get the desred reap
         * order. */
        pool->call<int>(
            io,
            sn1,
            interfacetype::test,
            timedelta::hours(1).future(),
            [] (serialise1 &s, connpool::connlock) {
                string().serialise(s); },
            [] (orerror<nnp<deserialise1> > d, connpool::connlock) ->
            orerror<int> {
                assert(string(*d.success()) == "");
                return 1023; });
        assert(nrreaped == 0);
        pool->call<int>(
            io,
            sn2,
            interfacetype::test,
            timedelta::hours(1).future(),
            [] (serialise1 &s, connpool::connlock) {
                string("foo").serialise(s); },
            [] (orerror<nnp<deserialise1> > d, connpool::connlock)
            -> orerror<int> {
                assert(string(*d.success()) == "foo");
                return 1023; });
        assert(nrreaped == 0);
        pool->call<int>(
            io,
            sn1,
            interfacetype::test,
            timedelta::hours(1).future(),
            [] (serialise1 &s, connpool::connlock) {
                string().serialise(s); },
            [] (orerror<nnp<deserialise1> > d, connpool::connlock)
            -> orerror<int> {
                assert(string(*d.success()) == "");
                return 1023; });
        assert(nrreaped == 0);
        while (nrreaped == 0)timedelta::milliseconds(10).future().sleep(io);
        srv2->destroy(io);
        srv1->destroy(io);
        pool->destroy(); },
    "multicall", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto srv(rpcservice2::listen<slowservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting slow service"));
        auto pool(connpool::build(cn).fatal("building pool"));
        auto startc1(timestamp::now());
        auto c1(pool->call(
                    sn,
                    interfacetype::test,
                    timedelta::milliseconds(200).future(),
                    [] (serialise1 &s, connpool::connlock) {
                        s.push(timedelta::hours(1));
                        s.push(1u); }));
        spark<void> s1([c1, startc1] {
                assert(c1->pop(clientio::CLIENTIO) == error::timeout);
                auto endc1(timestamp::now());
                tassert(T(endc1) - T(startc1) >= T(200_ms));
                tassert(T(endc1) - T(startc1) <= T(400_ms)); });
        auto startc2(timestamp::now());
        auto c2(pool->call(
                    sn,
                    interfacetype::test,
                    timedelta::milliseconds(50).future(),
                    [] (serialise1 &s, connpool::connlock) {
                        s.push(timedelta::hours(1));
                        s.push(1u); }));
        assert(c2->pop(io) == error::timeout);
        auto endc2(timestamp::now());
        tassert(T(endc2) - T(startc2) >= T(50_ms));
        tassert(T(endc2) - T(startc2) <= T(250_ms));
        s1.get();
        pool->destroy();
        srv->destroy(io); },
    /* This doesn't really belong here, but it's the easiest place to
     * put it. */
    "doublelisten", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto srv(rpcservice2::listen<echoservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting echo service"));
        auto port(srv->port());
        assert(
            rpcservice2::listen<echoservice>(io, cn, sn,peername::all(port))
            == error::from_errno(EADDRINUSE));
        srv->destroy(io);
        auto e(rpcservice2::listen<echoservice>(io,cn,sn,peername::all(port)));
        if (e.isfailure()) {
            e.failure().warn("restarting echo service on " + port.field());
            logmsg(loglevel::failure,
                   "netstat res " +
                   spawn::program("/bin/netstat")
                   .addarg("-a")
                   .addarg("-t")
                   .addarg("-n")
                   .addarg("-p")
                   .run(io)
                   .field());
            e.failure().fatal("failed"); }
        e.success()->destroy(io); },
    "faildeserialiseclient", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto srv(rpcservice2::listen<echoservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting echo service"));
        auto pool(connpool::build(cn).fatal("building pool"));
        assert(pool->call<void>(
                   io,
                   sn,
                   interfacetype::test,
                   Nothing,
                   [] (serialise1 &s, connpool::connlock) {
                       s.push(string("foo")); },
                   [] (orerror<nnp<deserialise1> > ds, connpool::connlock) {
                       if (ds.isfailure()) return ds.failure();
                       ds.success()->fail(error::toolate);
                       return error::toosoon; }) == error::toosoon);
        pool->destroy();
        srv->destroy(io); },
    "faildeserialiseserver", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto srv(rpcservice2::listen<echoservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting echo service"));
        auto pool(connpool::build(cn).fatal("building pool"));
        assert(pool->call<void>(
                   io,
                   sn,
                   interfacetype::test,
                   timedelta::milliseconds(100).future(),
                   [] (serialise1 &s, connpool::connlock) {
                       s.push((unsigned)-1); },
                   [] (orerror<nnp<deserialise1> > ds, connpool::connlock) {
                       if (ds.isfailure()) return ds.failure();
                       ds.success()->fail(error::toolate);
                       return error::toosoon; }) == error::underflowed);
        pool->destroy();
        srv->destroy(io); },
    "timeout", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto srv(rpcservice2::listen<echoservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting echo service"));
        auto pool(connpool::build(cn).fatal("building pool"));
        assert(pool->call<void>(
                   io,
                   sn,
                   interfacetype::test,
                   (-100_ms).future(),
                   [] (serialise1 &s, connpool::connlock) {
                       s.push((unsigned)-1); },
                   [] (orerror<nnp<deserialise1> > ds, connpool::connlock) {
                       assert(ds == error::timeout);
                       return error::toosoon; }) ==
               error::toosoon);
        pool->destroy();
        srv->destroy(io); },
    "pauseservice", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto srv(rpcservice2::listen<raceservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting slow service"));
        bool stopclient(false);
        unsigned nrcalls(0);
        spark<void> client([&cn, &sn, &stopclient, &nrcalls] {
                auto pool(connpool::build(cn).fatal("building pool"));
                list<nnp<connpool::asynccall> > outstanding;
                auto startnext([&] {
                        outstanding.append(
                            pool->call(sn,
                                       interfacetype::test,
                                       Nothing,
                                       [] (serialise1 &s, connpool::connlock) {
                                           s.push(1_ms); })); });
                for (unsigned x = 0; x < 10; x++) startnext();
                while (!outstanding.empty()) {
                    auto c(outstanding.pophead());
                    assert(c->pop(clientio::CLIENTIO).issuccess());
                    nrcalls++;
                    if (!stopclient) startnext(); }
                pool->destroy(); });
        (100_ms).future().sleep(io);
        tassert(T(nrcalls) > T(10u));
        auto start(timestamp::now());
        auto pt(srv->pause(io));
        srv->paused = true;
        auto end(timestamp::now());
        assert((end - start) < 100_ms);
        auto prepause(nrcalls);
        (100_ms).future().sleep(io);
        srv->paused = false;
        start  = timestamp::now();
        srv->unpause(pt);
        end = timestamp::now();
        assert(end - start < 100_ms);
        (100_ms).future().sleep(io);
        tassert(T(nrcalls) - T(prepause) > T(10u));
        /* We're done.  Shut it all down. */
        stopclient = true;
        client.get();
        srv->destroy(io); },
    "initialisefail", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        assert(rpcservice2::listen<failinitservice>(
                   io,
                   cn,
                   sn,
                   peername::all(peername::port::any))
               == error::toolate); },
    "asynccallconst", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto srv(rpcservice2::listen<echoservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting echo service"));
        auto pool(connpool::build(cn).fatal("starting conn pool"));
        auto ac(pool->call<int>(
                    sn,
                    interfacetype::test,
                    timestamp::now() + timedelta::hours(1),
                    [] (serialise1 &s, connpool::connlock) {
                        string("HELLO!").serialise(s); },
                    []
                    (deserialise1 &ds, connpool::connlock) -> orerror<int> {
                        string msg(ds);
                        unsigned cntr(ds);
                        assert(!ds.isfailure());
                        assert(msg == "HELLO!");
                        assert(cntr == 73);
                        return 12; }));
        const connpool::asynccallT<int> &constac(*ac);
        {   subscriber sub;
            subscription sc(sub, constac.pub());
            while (constac.finished() == Nothing) sub.wait(io); }
        assert(ac->finished() != Nothing);
        assert(ac->pop(ac->finished().just()).issuccess());
        pool->destroy();
        srv->destroy(io); },
    "deadlinepast", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto srv(rpcservice2::listen<echoservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting echo service"));
        auto pool(connpool::build(cn).fatal("starting conn pool"));
        assert(pool->call<void>(
                   io,
                   sn,
                   interfacetype::test,
                   timestamp::now() - 10_ms,
                   [] (serialise1 &s, connpool::connlock) {
                       string("").serialise(s); },
                   []
                   (deserialise1 &ds, connpool::connlock) -> orerror<void> {
                       (string)ds;
                       (unsigned)ds;
                       return Success; }) == error::timeout);
        pool->destroy();
        srv->destroy(io); },
    "inccallfield", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto srv(rpcservice2::listen<incompletecallfield>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting echo service"));
        auto pool(connpool::build(cn).fatal("starting conn pool"));
        assert(srv->msgs.length() == 0);
        pool->call<void>(io,
                         sn,
                         interfacetype::test,
                         Nothing,
                         [] (serialise1 &, connpool::connlock) {},
                         [] (deserialise1 &, connpool::connlock) ->
                             orerror<void> {
                             return Success; });
        assert(srv->msgs.length() == 1);
        pool->call<void>(io,
                         sn,
                         interfacetype::test,
                         Nothing,
                         [] (serialise1 &, connpool::connlock) {},
                         [] (deserialise1 &, connpool::connlock) ->
                             orerror<void> {
                             return Success; });
        assert(srv->msgs.length() == 2);
        assert(srv->msgs.idx(0) != srv->msgs.idx(1));
        srv->destroy(io);
        pool->destroy(); },
    "connbad", [] (clientio io) {
        /* Connect to something which isn't an rpcservice */
        auto l(socket_t::listen(peername::all(peernameport::any))
               .fatal("listening"));
        waitbox<void> done;
        spark<void> s(
            [l, &done] {
                subscriber sub;
                iosubscription ss1(sub, l.poll());
                subscription ss2(sub, done.pub());
                while (true) {
                    auto ss(sub.wait(clientio::CLIENTIO));
                    if (ss == &ss1) {
                        l.accept(clientio::CLIENTIO).fatal("accept").close(); }
                    else {
                        assert(ss == &ss2);
                        return; } } });
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname an(q);
        auto &bs(*beaconserver::build(
                     beaconserverconfig::dflt(cn, an),
                     empty,
                     l.localname().getport())
                 .fatal("starting beacon"));
        auto &cp(*connpool::build(cn).fatal("connpool"));
        assert(cp.call(io,
                       an,
                       interfacetype::test,
                       (1_s).future(),
                       [] (serialise1 &, connpool::connlock) {})
               == error::timeout);
        done.set();
        cp.destroy();
        bs.destroy(io);
        l.close(); },
    "destroybusy", [] (clientio io) {
        /* Should be able to destroy a connpool even when there are
         * calls outstanding. */
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname an(q);
        auto &srv(*rpcservice2::listen<slowservice>(
                      io,
                      cn,
                      an,
                      peername::all(peername::port::any))
                  .fatal("starting slow service"));
        auto &pool(*connpool::build(cn).fatal("building connpool"));
        bool completed(false);
        auto c(pool.call<void>(
                   an,
                   interfacetype::test,
                   Nothing,
                   [] (serialise1 &s, connpool::connlock) {
                       (10000_s).serialise(s);
                       s.push(1u); },
                   [&completed]
                   (connpool::asynccall &ac,
                    orerror<nnp<deserialise1> > d,
                    connpool::connlock) {
                       tassert(T(d.failure()) == T(error::disconnected));
                       assert(!completed);
                       completed = true;
                       return d.failure(); }));
        assert(!completed);
        tassert(T2(timedelta,
                   timedelta::time([&] { pool.destroy(); })) < T(1_s));
        auto t(timedelta::time([&] {
                    tassert(T(c->pop(clientio::CLIENTIO)) ==
                            T(error::disconnected)); }));
        tassert(T(t) < T(1_s));
        assert(completed);
        srv.destroy(io); },
#if TESTING
    "status", [] (clientio io) {
        /* Noddy test that a couple of simple cases don't outright
         * crash and actually generate some status messages. */
        unsigned nrmessages;
        nrmessages = 0;
        tests::eventwaiter< ::loglevel> waiter(
            tests::logmsg,
            [&nrmessages] (loglevel level) {
                if (level >= loglevel::info) nrmessages++; });
        auto _mustlog([&] (unsigned line,
                           const char *msg,
                           const std::function<void ()> &f) {
                logmsg(loglevel::info, fields::mk(line) + ":" + msg);
                nrmessages = 0;
                f();
                assert(nrmessages != 0); } );
#define mustlog(x) _mustlog(__LINE__, #x, [&] { x; })
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto srv(rpcservice2::listen<slowservice>(
                     io,
                     cn,
                     sn,
                     peername::all(peername::port::any))
                 .fatal("starting slow service"));
        mustlog(srv->status());
        auto pool(connpool::build(cn).fatal("building connpool"));
        mustlog(pool->status());
        auto ac(pool->call<void>(
                    sn,
                    interfacetype::test,
                    Nothing,
                    [] (serialise1 &s, connpool::connlock) {
                        (3600_s).serialise(s);
                        s.push((unsigned)12345678); },
                    []
                    (deserialise1 &d, connpool::connlock)
                    -> orerror<void> {
                        (unsigned)d;
                        return Success; }));
        mustlog(pool->status());
        mustlog(srv->status());
        ac->abort();
        mustlog(pool->status());
        mustlog(srv->status());
        srv->destroy(io);
        mustlog(pool->status());
        pool->destroy(); },
#endif
    "predictability", [] (clientio io) {
        /* We don't just need to be fast, we also need to be
         * predictable. The median and mean latency should be similar,
         * and the 99th percentile should be reasonably close. */
        /* Easier to get predictability with logging turned off. */
        maybe<logging::silence> s((Just()));
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        static unsigned nroutstanding = 200;
        auto &srv(*rpcservice2::listen<slowservice>(
                      io,
                      rpcservice2config(
                          beaconserverconfig::dflt(cn, sn),
                          nroutstanding+1,
                          10000000),
                      peername::all(peername::port::any))
                  .fatal("starting slow service"));
        auto &pool(*connpool::build(cn).fatal("building connpool"));
        list<timedelta> samples;
        subscriber sub;
        class call {
        public: nnp<connpool::asynccall> inner;
        public: timestamp started;
        public: maybe<subscription> ss;
        public: call(connpool &pool,
                     agentname sn,
                     subscriber &sub)
            : inner(pool.call(sn,
                              interfacetype::test,
                              Nothing,
                              /* Adding just a tiny little bit of fuzz
                               * increases the SD, but decreases skew
                               * and kurtosis, because there's less of
                               * a thundering herd problem. */
                              [] (serialise1 &s, connpool::connlock) {
                                  timedelta::milliseconds(95 + random() % 10)
                                      .serialise(s);
                                  s.push(1u); })),
              started(timestamp::now()),
              ss(Just(), sub, inner->pub(), this) {}
        public: bool pinged(list<timedelta> &samples) {
            auto t(inner->finished());
            if (t == Nothing) return false;
            samples.append(timestamp::now() - started);
            ss = Nothing;
            inner->pop(t.just()).fatal("getting call results");
            delete this;
            return true; } };
        /* Start off with @nroutstanding outstanding. Try to stagger
         * the start points a little to avoid thundering herd
         * effects. */
        {   auto stagger(100_ms / (double)nroutstanding);
            for (unsigned x = 0; x < nroutstanding; x++) {
                new call(pool, sn, sub);
                stagger.future().sleep(io); } }
        /* Run for 5 seconds. */
        auto deadline((5_s).future());
        while (deadline.infuture()) {
            auto ss(sub.wait(io, deadline));
            if (ss == NULL) continue;
            auto c((call *)ss->data);
            assert(c->ss.isjust());
            assert(ss == &c->ss.just());
            if (c->pinged(samples)) new call(pool, sn, sub); }
        /* Drain the outstanding calls. */
        {   unsigned x = 0;
            while (x < nroutstanding) {
                auto ss(sub.wait(io));
                auto c((call *)ss->data);
                assert(ss == &c->ss.just());
                if (c->pinged(samples)) x++; } }
        s = Nothing;
        pool.destroy();
        srv.destroy(io);
        /* Not much point in doing this if we don't have at least a
         * few hundred samples. */
        unsigned nrsamples(samples.length());
        tassert(T(nrsamples) > T(1500u));
        /* Drop the first and last few, because they're more likely to
         * have odd results. */
        for (unsigned x = 0; x < 500; x++) samples.drophead();
        for (unsigned x = 0; x < 500; x++) samples.droptail();
        nrsamples -= 1000;
        ::sort(samples);
        s = Nothing;
        timedelta total(0_s);
        for (auto it(samples.start()); !it.finished(); it.next()) total += *it;
        /* Not much point in doing this if we don't have at least a
         * few hundred samples. */
        tassert(T(nrsamples) > T(500u));
        double mean(total / (1_s * nrsamples));
        double moments[3];
        double p1;
        double p50;
        double p99;
        {   unsigned x = 0;
            auto it(samples.start());
            while (!it.finished()) {
                double ss = *it / 1_s;
                for (unsigned y = 0; y < ARRAYSIZE(moments); y++) {
                    moments[y] += pow(ss - mean, y + 2); }
                if (x == nrsamples / 100) p1 = ss;
                else if (x == nrsamples / 2) p50 = ss;
                else if (x == (nrsamples / 100) * 99) p99 = ss;
                it.next();
                x++; } }
        double var = moments[0] / nrsamples;
        double sd = pow(var, 0.5);
        double skew = moments[1] / (nrsamples * pow(var, 1.5));
        double kurtosis = moments[2] / (nrsamples * var * var) - 3;
        logmsg(loglevel::info, "nrsamples: " + fields::mk(nrsamples));
        logmsg(loglevel::info, "mean: " + fields::mk(mean));
        logmsg(loglevel::info,
               "1-50-99:" + fields::mk(p1) +
               " " + fields::mk(p50) +
               " " + fields::mk(p99));
        logmsg(loglevel::info,
               "sd-skew-kurt: " + fields::mk(sd) +
               "-" + fields::mk(skew) +
               "-" + fields::mk(kurtosis));
        /* A few simple checks that we're not too far from the
         * distribution we expect. */
        testassert::expression<bool> &res(
            /* Few extreme outliers */
            (T(p1) >= T(mean) - T(sd) * T(5.0)) &&
            (T(p99) <= T(mean) + T(sd) * T(5.0)) &&
            /* Median close to mean */
            (T(p50) <= T(mean) + T(sd)) &&
            (T(p50) >= T(mean) - T(sd)) &&
            /* Positive skew, give or take some margin of error. */
            (T(skew) >= T(-0.5)) &&
            /* But not too skewed */
            (T(skew) <= T(2.0)) &&
            /* Vaguely reasonable kurtosis. */
            (T(kurtosis) <= T(2.0)));
        if (!res.eval()) {
            logmsg(loglevel::emergency, "poor distribution:" + res.field());
            for (auto it(samples.start()); !it.finished(); it.next()) {
                logmsg(loglevel::info, "sample " + it->field()); }
            abort(); }
        delete &res; });
