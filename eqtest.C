#include "buffer.H"
#include "connpool.H"
#include "eqclient.H"
#include "eqserver.H"
#include "interfacetype.H"
#include "rpcservice2.H"
#include "test.H"

#include "rpcservice2.tmpl"

class eqtestserver : public rpcservice2 {
public: eqserver &eqs;
public: eqtestserver(const constoken &tok,
                     eqserver &_eqs)
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

void
tests::_eqtest() {
    testcaseIO("eq", "basic", [] (clientio io) {
            clustername cn((quickcheck()));
            slavename sn((quickcheck()));
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
            auto q(server->mkqueue(proto::eq::names::testunsigned));
            auto c(eqclient<unsigned>::connect(
                       io,
                       *pool,
                       sn,
                       proto::eq::names::testunsigned,
                       timestamp::now() + timedelta::seconds(10))
                   .fatal("connecting eqclient"));
            assert(c->pop() == Nothing);
            q->queue(52, rpcservice2::acquirestxlock(io));
            assert(timedelta::time([&] { assert(c->pop(io) == 52); })
                   < timedelta::milliseconds(200));
            assert(c->pop() == Nothing);
            assert(timedelta::time([&] {
                        q->destroy(rpcservice2::acquirestxlock(io)); })
                   < timedelta::milliseconds(100));
            assert(timedelta::time([&] { s->destroy(io); })
                   < timedelta::milliseconds(200));
            assert(timedelta::time([&] { server->destroy(); })
                   < timedelta::milliseconds(200));
            c->destroy(io);
            pool->destroy(); }); }
