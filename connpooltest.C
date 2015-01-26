#include "connpool.H"

#include "beaconclient.H"
#include "buffer.H"
#include "error.H"
#include "logging.H"
#include "quickcheck.H"
#include "rpcservice2.H"
#include "serialise.H"
#include "test.H"

#include "connpool.tmpl"
#include "list.tmpl"
#include "maybe.tmpl"
#include "rpcservice2.tmpl"
#include "spark.tmpl"
#include "test.tmpl"
#include "timedelta.tmpl"

namespace tests {

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

class slowservice : public rpcservice2 {
public: list<spark<void> > outstanding;
public: slowservice(const rpcservice2::constoken &t)
    : rpcservice2(t, interfacetype::test) {}
public: orerror<void> called(
    clientio,
    deserialise1 &ds,
    interfacetype,
    nnp<incompletecall> ic,
    onconnectionthread) final {
    timedelta delay(ds);
    unsigned key(ds);
    if (ds.isfailure()) return ds.failure();
    auto deadline(timestamp::now() + delay);
    outstanding.append([deadline, key, ic] {
            {   subscriber sub;
                subscription ss(sub, ic->abandoned().pub);
                while (deadline.infuture() && !ic->abandoned().ready()) {
                    sub.wait(clientio::CLIENTIO, deadline); } }
            ic->complete(
                [key] (serialise1 &s, mutex_t::token) { s.push(key); },
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
    (timestamp::now() + timedelta::milliseconds(10)).sleep(io);
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
                subscription ss(sub, ic->abandoned().pub);
                while (!ic->abandoned().ready()) sub.wait(clientio::CLIENTIO); }
            callaborted.set();
            ic->fail(error::toosoon, acquirestxlock(clientio::CLIENTIO)); });
    return Success; } }; }

void
tests::_connpool() {
    testcaseIO("connpool", "null", [] (clientio io) {
            /* Tests of what happens when there's nothing to connect
             * to. */
            clustername cn((quickcheck()));
            auto start(timestamp::now());
            auto pool(connpool::build(cn).fatal("starting conn pool"));
            assert(timestamp::now() < start + timedelta::milliseconds(100));
            start = timestamp::now();
            assert(pool->call(
                       io,
                       slavename("nonesuch"),
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
                           return error::ratelimit; })
                   == error::ratelimit);
            auto end(timestamp::now());
            assert(end > start + timedelta::milliseconds(10));
            assert(end < start + timedelta::milliseconds(110));
            start = timestamp::now();
            auto c(pool->call(
                       slavename("nonesuch"),
                       interfacetype::test,
                       timestamp::now() + timedelta::hours(10),
                       [] (serialise1 &, connpool::connlock) { abort(); },
                       []
                       (orerror<nnp<deserialise1> > e, connpool::connlock)
                           -> orerror<void> {
                           assert(e == error::aborted);
                           return error::ratelimit; }));
            end = timestamp::now();
            /* Starting the call should be very cheap. */
            assert(end < start + timedelta::milliseconds(10));
            start = end;
            assert(c->abort() == error::ratelimit);
            end = timestamp::now();
            /* Aborting can be a little more expensive, but not
             * much. */
            assert(end < start + timedelta::milliseconds(50));
            c = pool->call(
                slavename("nonesuch"),
                interfacetype::test,
                timestamp::now() + timedelta::hours(10),
                [] (serialise1 &, connpool::connlock) { abort(); },
                []
                (orerror<nnp<deserialise1> > e, connpool::connlock)
                    -> orerror<void> {
                    assert(e == error::disconnected);
                    return error::ratelimit; });
            start = timestamp::now();
            pool->destroy();
            end = timestamp::now();
            assert(end - start < timedelta::milliseconds(50));
            start = end;
            assert(c->pop(io) == error::ratelimit);
            end = timestamp::now();
            assert(end - start < timedelta::milliseconds(10)); });
    testcaseIO("connpool", "echo", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
            auto srv(rpcservice2::listen<echoservice>(
                         io,
                         cn,
                         sn,
                         peername::all(peername::port::any))
                     .fatal("starting echo service"));
            auto pool(connpool::build(cn).fatal("starting conn pool"));
            pool->call(
                io,
                sn,
                interfacetype::test,
                timestamp::now() + timedelta::hours(1),
                [] (serialise1 &s, connpool::connlock) {
                    string("HELLO!").serialise(s); },
                []
                (deserialise1 &ds, connpool::connlock)
                    -> orerror<void> {
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
                    (deserialise1 &ds, connpool::connlock) ->
                        orerror<int> {
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
            srv->destroy(io); });
    testcaseIO("connpool", "abandon1", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
            waitbox<void> abandoned;
            auto srv(rpcservice2::listen<abandonservice>(
                         io,
                         cn,
                         sn,
                         peername::all(peername::port::any),
                         abandoned)
                     .fatal("starting abandon service"));
            auto pool(connpool::build(cn).fatal("building connpool"));
            auto call(pool->call(
                          sn,
                          interfacetype::test,
                          timestamp::now() + timedelta::hours(1),
                          [] (serialise1 &, connpool::connlock) {},
                          [] (connpool::asynccall &,
                              orerror<nnp<deserialise1> > d,
                              connpool::connlock) {
                              assert(d == error::disconnected);
                              return error::toosoon; }));
            (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
            assert(call->finished() == Nothing);
            pool->destroy();
            assert(call->pop(io) == error::toosoon);
            abandoned.get(io);
            srv->destroy(io); });
    testcaseIO("connpool", "timeoutcall", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
            waitbox<void> abandoned;
            auto srv(rpcservice2::listen<abandonservice>(
                         io,
                         cn,
                         sn,
                         peername::all(peername::port::any),
                         abandoned)
                     .fatal("starting abandon service"));
            auto pool(connpool::build(cn).fatal("building connpool"));
            assert(pool->call(
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
            pool->destroy(); });
    testcaseIO("connpool", "slow", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
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
            auto call1(pool->call(
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
            auto call2(pool->call(
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
            srv->destroy(io); });
    testcaseIO("connpool", "slowabandon", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
            auto srv(rpcservice2::listen<slowservice>(
                         io,
                         cn,
                         sn,
                         peername::all(peername::port::any))
                     .fatal("starting slow service"));
            auto pool(connpool::build(cn).fatal("building connpool"));
            auto call(pool->call(
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
            assert(timedelta::time([srv, io] { srv->destroy(io); }) <
                   timedelta::milliseconds(100));
            assert(timedelta::time([call, io] { call->abort(); }) <
                   timedelta::milliseconds(100));
            assert(timedelta::time([pool] { pool->destroy(); }) <
                   timedelta::milliseconds(100)); });
    testcaseIO("connpool", "abortcompleted", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
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
            while (call->finished() == Nothing) {
                (timestamp::now() + timedelta::milliseconds(1)).sleep(io); }
            ::logmsg(loglevel::info, "aborting");
            assert(call->abort() == 1023);
            ::logmsg(loglevel::info, "aborted");
            pool->destroy();
            srv->destroy(io); });
    testcaseIO("connpool", "clientdisco", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
            waitbox<void> died;
            hook<void> h(rpcservice2::clientdisconnected,
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
            assert(pool2->call(
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
            assert(timedelta::time([&died, io] { died.get(io); })
                   < timedelta::milliseconds(100));
            pool1->destroy();
            srv->destroy(io); });
    testcaseIO("connpool", "largeresp", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
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
                    pool->call(
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
            srv->destroy(io); });
    testcaseIO("connpool", "largereq", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
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
                    pool->call(
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
            srv->destroy(io); });
    testcaseIO("connpool", "returnbuffer", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
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
            srv->destroy(io); });
    testcaseIO("connpool", "slowcall", [] (clientio io) {
            /* The conn pool shouldn't drop connections which still
             * have outstanding calls. */
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
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
            pool->call(
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
            assert(finished.just() - start > timedelta::milliseconds(200));
            /* But not too far past it. */
            assert(end - finished.just() < timedelta::milliseconds(20));
            assert(end - start < timedelta::milliseconds(250));
            pool->destroy();
            srv->destroy(io); });
    testcaseIO("connpool", "abort1", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
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
            auto b(pool->call(
                       sn,
                       interfacetype::test,
                       timedelta::hours(1).future(),
                       [] (serialise1 &, connpool::connlock) {},
                       [] (deserialise1 &, connpool::connlock)
                           -> orerror<void> {
                           abort(); }));
            callstarted.get(io);
            assert(b->abort() == error::aborted);
            assert(timedelta::time([&callaborted, io] { callaborted.get(io); })
                   < timedelta::milliseconds(50));
            pool->destroy();
            srv->destroy(io); });
    testcaseIO("connpool", "abort2", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
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
            srv->destroy(io); });
    testcaseV("connpool", "config", [] {
            quickcheck q;
            beaconclientconfig bcc((clustername(q)));
            assert(connpool::config::mk(bcc,
                                        probability::never,
                                        timedelta::seconds(-1)) ==
                   error::invalidparameter);
            assert(connpool::config(bcc) == connpool::config::mk(bcc)); });
    testcaseIO("connpool", "connreap", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn1(q);
            slavename sn2(q);
            ::logmsg(loglevel::debug, "slave name 1 " + fields::mk(sn1));
            ::logmsg(loglevel::debug, "slave name 2 " + fields::mk(sn2));
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
                [] (orerror<nnp<deserialise1> > d,
                    connpool::connlock) -> orerror<int> {
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
                [] (orerror<nnp<deserialise1> > d,
                    connpool::connlock) -> orerror<int> {
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
                [] (orerror<nnp<deserialise1> > d,
                    connpool::connlock) -> orerror<int> {
                    assert(string(*d.success()) == "");
                    return 1023; });
            assert(nrreaped == 0);
            while (nrreaped == 0)timedelta::milliseconds(10).future().sleep(io);
            srv2->destroy(io);
            srv1->destroy(io);
            pool->destroy(); });
    testcaseIO("connpool", "multicall", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
            auto srv(rpcservice2::listen<slowservice>(
                         io,
                         cn,
                         sn,
                         peername::all(peername::port::any))
                     .fatal("starting slow service"));
            auto pool(connpool::build(cn).fatal("building pool"));
            auto c1(pool->call(
                        sn,
                        interfacetype::test,
                        timedelta::milliseconds(100).future(),
                        [] (serialise1 &s, connpool::connlock) {
                            s.push(timedelta::hours(1));
                            s.push(1u); },
                        connpool::voidcallV));
            spark<void> s1([c1] {
                    auto t(
                        timedelta::time(
                            [c1] { c1->pop(clientio::CLIENTIO); }));
                    assert(t >= timedelta::milliseconds(50));
                    assert(t <= timedelta::milliseconds(150)); });
            auto c2(pool->call(
                        sn,
                        interfacetype::test,
                        timedelta::milliseconds(50).future(),
                        [] (serialise1 &s, connpool::connlock) {
                            s.push(timedelta::hours(1));
                            s.push(1u); },
                        connpool::voidcallV));
            auto t(timedelta::time([c2, io] { c2->pop(io); }));
            assert(t >= timedelta::milliseconds(40));
            assert(t < timedelta::milliseconds(100));
            s1.get();
            pool->destroy();
            srv->destroy(io); });
            /* This doesn't really belong here, but it's the easiest
             * place to put it. */
    testcaseIO("connpool", "doublelisten", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
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
            srv = rpcservice2::listen<echoservice>(io,cn,sn,peername::all(port))
                .fatal("restarting echo service");
            srv->destroy(io); } ); }
