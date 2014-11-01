#include "test.H"

#include "logging.H"
#include "nnp.H"
#include "rpcclient2.H"
#include "rpcservice2.H"
#include "serialise.H"
#include "spark.H"
#include "string.H"
#include "timedelta.H"

#include "maybe.tmpl"
#include "rpcclient2.tmpl"
#include "rpcservice2.tmpl"
#include "spark.tmpl"

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
} }
