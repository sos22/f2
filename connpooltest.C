#include "connpool.H"

#include "beaconclient.H"
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
#include "timedelta.tmpl"

namespace tests {

class echoservice2 : public rpcservice2 {
private: unsigned cntr;
public:  echoservice2(const rpcservice2::constoken &t)
    : rpcservice2(t, interfacetype::test),
      cntr(73) {}
public: orerror<void> called(
    clientio,
    onconnectionthread oct,
    deserialise1 &ds,
    interfacetype,
    nnp<incompletecall> ic) final {
    string msg(ds);
    if (ds.isfailure()) return ds.failure();
    if (cntr == 75) {
        cntr++;
        ic->fail(error::toolate, oct); }
    else {
        ic->complete(
            [&msg, this] (serialise1 &s,
                          mutex_t::token,
                          onconnectionthread) {
                msg.serialise(s);
                s.push(cntr++); },
            oct); }
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
    onconnectionthread,
    deserialise1 &,
    interfacetype,
    nnp<incompletecall> ic) final {
    worker.mkjust([this, ic] () {
            ic->abandoned().get(clientio::CLIENTIO);
            abandoned.set();
            ic->fail(error::toolate); });
    return Success; } };

class slowservice : public rpcservice2 {
public: list<spark<void> > outstanding;
public: slowservice(const rpcservice2::constoken &t)
    : rpcservice2(t, interfacetype::test) {}
public: orerror<void> called(
    clientio,
    onconnectionthread,
    deserialise1 &ds,
    interfacetype,
    nnp<incompletecall> ic) final {
    timedelta delay(ds);
    unsigned key(ds);
    if (ds.isfailure()) return ds.failure();
    auto deadline(timestamp::now() + delay);
    outstanding.append([deadline, key, ic] {
            {   subscriber sub;
                subscription ss(sub, ic->abandoned().pub);
                while (deadline.infuture() && !ic->abandoned().ready()) {
                    sub.wait(clientio::CLIENTIO, deadline); } }
            ic->complete([key] (serialise1 &s, mutex_t::token) {
                    s.push(key); }); });
    return Success; } };

class largerespservice : public rpcservice2 {
public: string largestring;
public: largerespservice(const constoken &t)
    : rpcservice2(t, interfacetype::test),
      largestring("Hello world, this is a test pattern") {
    /* Make it about eight megs. */
    for (unsigned x = 0; x < 18; x++) largestring = largestring + largestring; }
public: orerror<void> called(
    clientio,
    onconnectionthread oct,
    deserialise1 &,
    interfacetype,
    nnp<incompletecall> ic) final {
    ic->complete(
        [this] (serialise1 &s, mutex_t::token, onconnectionthread) {
            largestring.serialise(s); },
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
    onconnectionthread oct,
    deserialise1 &ds,
    interfacetype,
    nnp<incompletecall> ic) final {
    string s(ds);
    assert(s == largestring);
    /* Slow down thread to make it a bit easier for the client to fill their
     * TX buffer. */
    (timestamp::now() + timedelta::milliseconds(10)).sleep(io);
    ic->complete(
        [this] (serialise1 &, mutex_t::token, onconnectionthread) {},
        oct);
    return Success; } };

}

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
                       (connpool::asynccall &,
                        orerror<nnp<deserialise1> > e,
                        connpool::connlock)
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
                       (connpool::asynccall &,
                        orerror<nnp<deserialise1> > e,
                        connpool::connlock)
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
                (connpool::asynccall &,
                 orerror<nnp<deserialise1> > e,
                 connpool::connlock)
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
            auto srv(rpcservice2::listen<echoservice2>(
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
                (connpool::asynccall &, deserialise1 &ds, connpool::connlock)
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
                    (connpool::asynccallT<int> &,
                     deserialise1 &ds,
                     connpool::connlock) ->
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
                []
                (connpool::asynccallT<char *> &,
                 deserialise1 &,
                 connpool::connlock)
                    -> orerror<char *> {
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
                    []
                    (connpool::asynccallT<char *> &,
                     deserialise1 &ds,
                     connpool::connlock)
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
                       [] (connpool::asynccall &,
                           deserialise1 &,
                           connpool::connlock)
                           -> orerror<void> { abort(); })
                   == error::timeout);
            (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
            assert(!abandoned.ready());
            srv->destroy(io);
            abandoned.get(io);
            pool->destroy(); });
    testcaseIO("connpool", "slow", [] (clientio io) {
            initlogging("T");
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
            auto srv(rpcservice2::listen<echoservice2>(
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
            auto srv(rpcservice2::listen<echoservice2>(
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
                       []
                       (connpool::asynccallT<int> &,
                        deserialise1 &ds,
                        connpool::connlock) ->
                           orerror<int> {
                           string msg(ds);
                           unsigned cntr(ds);
                           assert(!ds.isfailure());
                           assert(msg == "");
                           assert(cntr == 74);
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
                       []
                       (connpool::asynccall &,
                        deserialise1 &ds,
                        connpool::connlock) ->
                           orerror<void> {
                           string msg(ds);
                           unsigned cntr(ds);
                           assert(!ds.isfailure());
                           assert(msg == "");
                           assert(cntr == 75);
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
            auto pool(connpool::build(cn).fatal("building pool1"));
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
}
