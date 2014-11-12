#include "test.H"

#include <arpa/inet.h>

#include "logging.H"
#include "nnp.H"
#include "rpcclient2.H"
#include "rpcservice2.H"
#include "serialise.H"
#include "spark.H"
#include "string.H"
#include "timedelta.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "rpcclient2.tmpl"
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

class bufferservice : public rpcservice2 {
public: bufferservice(const constoken &t) : rpcservice2(t,interfacetype::test){}
public: orerror<void> called(
    clientio,
    onconnectionthread oct,
    deserialise1 &ds,
    interfacetype,
    nnp<incompletecall> ic) final {
    ::buffer b(ds);
    assert(!memcmp(b.linearise(0, 5), "HELLO", 5));
    ic->complete(
        [] (serialise1 &s, mutex_t::token /* txlock */, onconnectionthread) {
            ::buffer bb;
            bb.queue("GOODBYE", 7);
            bb.serialise(s); },
        oct);
    return Success; } };

void
rpctest2() {
    testcaseIO("rpctest2", "echo", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
            auto srv(rpcservice2::listen<echoservice>(
                         io,
                         cn,
                         sn,
                         peername::all(peername::port::any))
                     .fatal("starting echo service"));
            auto clnt(rpcclient2::connect(
                          io,
                          peername::loopback(srv->port()))
                      .fatal("connecting to echo service"));
            clnt->call<void>(
                io,
                interfacetype::test,
                [] (serialise1 &s, mutex_t::token) {
                    string("HELLO!").serialise(s); },
                []
                (deserialise1 &ds, rpcclient2::onconnectionthread) ->
                    orerror<void> {
                    string msg(ds);
                    unsigned cntr(ds);
                    assert(!ds.isfailure());
                    assert(msg == "HELLO!");
                    assert(cntr == 73);
                    return Success; })
                .fatal("calling echo service");
            assert(
                clnt->call<int>(
                    io,
                    interfacetype::test,
                    [] (serialise1 &s, mutex_t::token) {
                        string("GOODBYE!").serialise(s); },
                    []
                    (deserialise1 &ds, rpcclient2::onconnectionthread) ->
                        orerror<int> {
                        string msg(ds);
                        unsigned cntr(ds);
                        assert(!ds.isfailure());
                        assert(msg == "GOODBYE!");
                        assert(cntr == 74);
                        return 9; })
                == 9);
            auto r = clnt->call<char *>(
                io,
                interfacetype::test,
                [] (serialise1 &s, mutex_t::token) {
                    string("GOODBYE!").serialise(s); },
                []
                (deserialise1 &, rpcclient2::onconnectionthread) ->
                orerror<char *> { abort(); } );
            assert(r == error::toolate);
            assert(
                clnt->call<char *>(
                    io,
                    interfacetype::test,
                    [] (serialise1 &s, mutex_t::token) {
                        string("boo").serialise(s); },
                    []
                    (deserialise1 &ds, rpcclient2::onconnectionthread)->
                    orerror<char *>{
                        string msg(ds);
                        unsigned cntr(ds);
                        assert(!ds.isfailure());
                        assert(msg == "boo");
                        assert(cntr == 76);
                        return (char *)7; })
                == (char *)7);
            clnt->destroy();
            srv->destroy(io); });
    testcaseIO("rpctest2", "doublelisten", [] (clientio io) {
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
            srv->destroy(io); } );
    testcaseIO("rpctest2", "abandon1", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
            waitbox<void> abandoned;
            auto srv(rpcservice2::listen<abandonservice>(
                         io,
                         cn,
                         sn,
                         peername::loopback(peername::port::any),
                         abandoned)
                     .fatal("starting abandon service"));
            auto clnt(rpcclient2::connect(
                          io,
                          peername::loopback(srv->port()))
                      .fatal("connecting to abandon service"));
            auto call(clnt->call<void>(
                          interfacetype::test,
                          [] (serialise1 &, mutex_t::token) {},
                          [] (rpcclient2::asynccall<void> &,
                              orerror<nnp<deserialise1> > d,
                              rpcclient2::onconnectionthread) {
                              assert(d == error::shutdown);
                              return error::toosoon; }));
            (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
            assert(call->finished() == Nothing);
            assert(call->abort(io) == Nothing);
            clnt->destroy();
            abandoned.get(io);
            srv->destroy(io); });
    testcaseIO("rpctest2", "timeoutcall", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
            waitbox<void> abandoned;
            auto srv(rpcservice2::listen<abandonservice>(
                         io,
                         cn,
                         sn,
                         peername::loopback(peername::port::any),
                         abandoned)
                     .fatal("starting abandon service"));
            auto clnt(rpcclient2::connect(
                          io,
                          peername::loopback(srv->port()))
                      .fatal("connecting to abandon service"));
            assert(clnt->call<void>(
                       io,
                       interfacetype::test,
                       [] (serialise1 &, mutex_t::token) {},
                       [] (deserialise1 &, rpcclient2::onconnectionthread)
                           -> orerror<void> { abort(); },
                       (timestamp::now() + timedelta::milliseconds(100)))
                   == error::timeout);
            (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
            assert(!abandoned.ready());
            srv->destroy(io);
            abandoned.get(io);
            clnt->destroy(); });
    testcaseIO("rpctest2", "slow", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
            auto srv(rpcservice2::listen<slowservice>(
                         io,
                         cn,
                         sn,
                         peername::loopback(peername::port::any))
                     .fatal("starting slow service"));
            auto clnt(rpcclient2::connect(io,
                                          peername::loopback(srv->port()))
                      .fatal("connecting to slow service"));
            maybe<timestamp> completed1(Nothing);
            maybe<timestamp> completed2(Nothing);
            list<nnp<rpcclient2::asynccall<void> > > completed;
            auto call1(clnt->call<void>(
                           interfacetype::test,
                           [] (serialise1 &s, mutex_t::token) {
                               timedelta::milliseconds(200).serialise(s);
                               s.push((unsigned)1); },
                           [&completed, &completed1]
                           (rpcclient2::asynccall<void> &ac,
                            orerror<nnp<deserialise1> > d,
                            rpcclient2::onconnectionthread)
                           -> orerror<void> {
                               d.fatal("getting response from slow service");
                               unsigned k(*d.success());
                               assert(k == 1);
                               assert(completed1 == Nothing);
                               completed1 = timestamp::now();
                               completed.pushtail(ac);
                               return Success; } ) );
            auto call2(clnt->call<void>(
                           interfacetype::test,
                           [] (serialise1 &s, mutex_t::token) {
                               timedelta::milliseconds(100).serialise(s);
                               s.push((unsigned)2); },
                           [&completed, &completed2]
                           (rpcclient2::asynccall<void> &ac,
                            orerror<nnp<deserialise1> > d,
                            rpcclient2::onconnectionthread)
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
            clnt->destroy();
            srv->destroy(io); });
    testcaseIO("rpctest2", "slowabandon", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
            auto srv(rpcservice2::listen<slowservice>(
                         io,
                         cn,
                         sn,
                         peername::loopback(peername::port::any))
                     .fatal("starting slow service"));
            auto clnt(rpcclient2::connect(io,
                                          peername::loopback(srv->port()))
                      .fatal("connecting to slow service"));
            auto call(clnt->call<void>(
                          interfacetype::test,
                          [] (serialise1 &s, mutex_t::token) {
                              timedelta::hours(3).serialise(s);
                              s.push((unsigned)3); },
                          [] (rpcclient2::asynccall<void> &,
                              orerror<nnp<deserialise1> > d,
                              rpcclient2::onconnectionthread) -> orerror<void> {
                              assert(d == error::disconnected);
                              return Success; }) );
            /* Wait for the call to start. */
            while (srv->outstanding.empty()) {
                (timestamp::now() + timedelta::milliseconds(1)).sleep(io); }
            /* Shut down server while it's got outstanding calls.
             * Should be able to do it quickly. */
            assert(timedelta::time([srv, io] { srv->destroy(io); }) <
                   timedelta::milliseconds(100));
            assert(timedelta::time([call, io] { call->abort(io); }) <
                   timedelta::milliseconds(100));
            assert(timedelta::time([clnt] { clnt->destroy(); }) <
                   timedelta::milliseconds(100)); });
    testcaseIO("rpctest2", "abortcompleted", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
            auto srv(rpcservice2::listen<echoservice>(
                         io,
                         cn,
                         sn,
                         peername::loopback(peername::port::any))
                     .fatal("starting echo service"));
            auto clnt(rpcclient2::connect(io,
                                          peername::loopback(srv->port()))
                      .fatal("connecting to echo service"));
            ::logmsg(loglevel::info, "send call");
            auto call(clnt->call<int>(
                          interfacetype::test,
                          [] (serialise1 &s, mutex_t::token) {
                              string("foo").serialise(s); },
                          [] (rpcclient2::asynccall<int> &,
                              orerror<nnp<deserialise1> > d,
                              rpcclient2::onconnectionthread) -> orerror<int> {
                              assert(string(*d.success()) == "foo");
                              return 1023; }));
            while (call->finished() == Nothing) {
                (timestamp::now() + timedelta::milliseconds(1)).sleep(io); }
            ::logmsg(loglevel::info, "aborting");
            assert(call->abort(io).just() == 1023);
            ::logmsg(loglevel::info, "aborted");
            clnt->destroy();
            srv->destroy(io); });
    testcaseIO("rpctest2", "clientdisco", [] (clientio io) {
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
                         peername::loopback(peername::port::any))
                     .fatal("starting echo service"));
            auto clnt1(rpcclient2::connect(io, peername::loopback(srv->port()))
                       .fatal("connecting to echo service"));
            auto clnt2(rpcclient2::connect(io, peername::loopback(srv->port()))
                       .fatal("connecting to echo service"));
            clnt2->destroy();
            assert(timedelta::time([&died, io] { died.get(io); })
                   < timedelta::milliseconds(100));
            clnt1->destroy();
            srv->destroy(io); });
    testcaseIO("rpctest2", "largeresp", [] (clientio io) {
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
                         peername::loopback(peername::port::any))
                     .fatal("starting large response service"));
            auto clnt(rpcclient2::connect(io, peername::loopback(srv->port()))
                      .fatal("connecting to large response service"));
            list<nnp<rpcclient2::asynccall<void> > > outstanding;
            for (unsigned x = 0; x < 10; x++) {
                outstanding.pushtail(
                    clnt->call<void>(
                        interfacetype::test,
                        [] (serialise1 &, mutex_t::token) {},
                        [srv, io] (rpcclient2::asynccall<void> &,
                                   orerror<nnp<deserialise1> > d,
                                   rpcclient2::onconnectionthread)
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
            clnt->destroy();
            srv->destroy(io); });
    testcaseIO("rpctest2", "largereq", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
            auto srv(rpcservice2::listen<largereqservice>(
                         io,
                         cn,
                         sn,
                         peername::loopback(peername::port::any))
                     .fatal("starting large request service"));
            auto clnt(rpcclient2::connect(io, peername::loopback(srv->port()))
                      .fatal("connecting to large request service"));
            list<nnp<rpcclient2::asynccall<void> > > outstanding;
            for (unsigned x = 0; x < 10; x++) {
                outstanding.pushtail(
                    clnt->call<void>(
                        interfacetype::test,
                        [srv] (serialise1 &s, mutex_t::token) {
                            srv->largestring.serialise(s); },
                        [srv, io] (rpcclient2::asynccall<void> &,
                                   orerror<nnp<deserialise1> > d,
                                   rpcclient2::onconnectionthread)
                        -> orerror<void> {
                            assert(d.issuccess());
                            return Success; })); }
            while (!outstanding.empty()) {
                assert(outstanding.pophead()->pop(io) == Success); }
            clnt->destroy();
            srv->destroy(io); });
    testcaseIO("rpctest2", "badconnect", [] (clientio io) {
            /* Make up a peername which probably respond quickly. */
            struct sockaddr_in sa;
            memset(&sa, 0, sizeof(sa));
            sa.sin_family = AF_INET;
            sa.sin_addr.s_addr = 0x08090a0b;
            sa.sin_port = 12345;
            assert(rpcclient2::connect(
                       io,
                       peername((struct sockaddr *)&sa, sizeof(sa)),
                       timestamp::now()) == error::timeout);
            assert(rpcclient2::connect(
                       io,
                       peername::loopback(peername::port(1)))
                   == error::from_errno(ECONNREFUSED)); });
    testcaseIO("rpctest2", "abortconnect", [] (clientio io) {
            /* Arrange to abort after doing the connect syscall but
             * before we do the HELLO */
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
            waitbox<void> doneconnect;
            tests::hook<void> h(
                rpcclient2::doneconnectsyscall,
                [&doneconnect] () {
                    doneconnect.set(); });
            auto srv(rpcservice2::listen<echoservice>(
                         io,
                         cn,
                         sn,
                         peername::loopback(peername::port::any))
                     .fatal("starting echo service"));
            auto conn(rpcclient2::connect(peername::loopback(srv->port())));
            doneconnect.get(io);
            conn->abort();
            srv->destroy(io); });
    testcaseIO("rpctest2", "returnbuffer", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
            auto srv(rpcservice2::listen<bufferservice>(
                         io,
                         cn,
                         sn,
                         peername::loopback(peername::port::any))
                     .fatal("starting buffer service"));
            auto conn(rpcclient2::connect(
                          io,
                          peername::loopback(srv->port()))
                      .fatal("connecting to buffer service"));
            auto b(conn->call< ::buffer >(
                       io,
                       interfacetype::test,
                       [] (serialise1 &s, mutex_t::token /* txlock */) {
                           ::buffer buf;
                           buf.queue("HELLO", 5);
                           buf.serialise(s); },
                       [] (deserialise1 &s, rpcclient2::onconnectionthread)
                           -> orerror< ::buffer> {
                           return (::buffer)s; })
                   .fatal("calling buffer service"));
            assert(!memcmp(b.linearise(0, 7), "GOODBYE", 7));
            conn->destroy();
            srv->destroy(io); });
} }
