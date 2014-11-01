#include "test.H"

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
#include "timedelta.tmpl"

namespace tests {

class echoservice : public rpcservice2 {
private: unsigned cntr;
public:  echoservice(const rpcservice2::constoken &t)
    : rpcservice2(t),
      cntr(73) {}
public: orerror<void> called(
    clientio,
    onconnectionthread oct,
    deserialise1 &ds,
    nnp<incompletecall> ic) {
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
    : rpcservice2(t),
      worker(Nothing),
      abandoned(_abandoned) {}
public: orerror<void> called(
    clientio,
    onconnectionthread,
    deserialise1 &,
    nnp<incompletecall> ic) {
    worker.mkjust([this, ic] () {
            ic->abandoned().get(clientio::CLIENTIO);
            abandoned.set();
            ic->fail(error::toolate); });
    return Success; } };

class slowservice : public rpcservice2 {
public: list<spark<void> > outstanding;
public: slowservice(const rpcservice2::constoken &t)
    : rpcservice2(t) {}
public: orerror<void> called(
    clientio,
    onconnectionthread,
    deserialise1 &ds,
    nnp<incompletecall> ic) {
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
    return Success; }
};

void
rpctest2() {
    testcaseIO("rpctest2", "echo", [] (clientio io) {
            auto srv(rpcservice2::listen<echoservice>(
                         peername::all(peername::port::any))
                     .fatal("starting echo service"));
            auto clnt(rpcclient2::connect(
                          io,
                          peername::loopback(srv->port()))
                      .fatal("connecting to echo service"));
            clnt->call<void>(
                io,
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
                [] (serialise1 &s, mutex_t::token) {
                    string("GOODBYE!").serialise(s); },
                []
                (deserialise1 &, rpcclient2::onconnectionthread) ->
                orerror<char *> { abort(); } );
            assert(r == error::toolate);
            assert(
                clnt->call<char *>(
                    io,
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
            auto srv(rpcservice2::listen<echoservice>(
                         peername::all(peername::port::any))
                     .fatal("starting echo service"));
            auto port(srv->port());
            assert(rpcservice2::listen<echoservice>(peername::all(port))
                   == error::from_errno(EADDRINUSE));
            srv->destroy(io);
            srv = rpcservice2::listen<echoservice>(peername::all(port))
                .fatal("restarting echo service");
            srv->destroy(io); } );
    testcaseIO("rpctest2", "abandon1", [] (clientio io) {
            waitbox<void> abandoned;
            auto srv(rpcservice2::listen<abandonservice>(
                         peername::loopback(peername::port::any),
                         abandoned)
                     .fatal("starting abandon service"));
            auto clnt(rpcclient2::connect(
                          io,
                          peername::loopback(srv->port()))
                      .fatal("connecting to abandon service"));
            auto call(clnt->call<void>(
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
            waitbox<void> abandoned;
            auto srv(rpcservice2::listen<abandonservice>(
                         peername::loopback(peername::port::any),
                         abandoned)
                     .fatal("starting abandon service"));
            auto clnt(rpcclient2::connect(
                          io,
                          peername::loopback(srv->port()))
                      .fatal("connecting to abandon service"));
            assert(clnt->call<void>(
                       io,
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
            auto srv(rpcservice2::listen<slowservice>(
                         peername::loopback(peername::port::any))
                     .fatal("starting slow service"));
            auto clnt(rpcclient2::connect(io, peername::loopback(srv->port()))
                      .fatal("connecting to slow service"));
            maybe<timestamp> completed1(Nothing);
            maybe<timestamp> completed2(Nothing);
            list<nnp<rpcclient2::asynccall<void> > > completed;
            auto call1(clnt->call<void>(
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
    testcaseIO("rpctest2", "slowabort", [] (clientio io) {
            auto srv(rpcservice2::listen<slowservice>(
                         peername::loopback(peername::port::any))
                     .fatal("starting slow service"));
            auto clnt(rpcclient2::connect(io, peername::loopback(srv->port()))
                      .fatal("connecting to slow service"));
            auto call(clnt->call<void>(
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
} }
