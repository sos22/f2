#include "test.H"

#include "logging.H"
#include "nnp.H"
#include "rpcclient2.H"
#include "rpcservice2.H"
#include "serialise.H"
#include "string.H"

#include "rpcclient2.tmpl"
#include "rpcservice2.tmpl"

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
    ic->complete(
        [&msg, this] (serialise1 &s,
                mutex_t::token,
                onconnectionthread) {
            msg.serialise(s);
            s.push(cntr++); },
        oct);
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
                (deserialise1 &ds, rpcclient2::onconnectionthread)->
                    orerror<void>{
                    string msg(ds);
                    unsigned cntr(ds);
                    assert(!ds.isfailure());
                    assert(msg == "HELLO!");
                    assert(cntr == 73);
                    return Success; })
                .fatal("calling echo service");
            clnt->destroy();
            srv->destroy(io); }); } }
