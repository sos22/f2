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

static void
eqtestcase(clientio io,
           const std::function<void (clientio,
                                     eqclient<unsigned> &,
                                     eventqueue<unsigned> &)> &f,
           const eventqueueconfig &qconf = eventqueueconfig::dflt()) {
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
    auto q(server->mkqueue(proto::eq::names::testunsigned, qconf));
    auto c(eqclient<unsigned>::connect(
               io,
               *pool,
               sn,
               proto::eq::names::testunsigned,
               timestamp::now() + timedelta::seconds(10))
           .fatal("connecting eqclient"));
    f(io, *c, *q);
    c->destroy(io);
    pool->destroy();
    assert(timedelta::time([&] { s->destroy(io); })
           < timedelta::milliseconds(200));
    assert(timedelta::time([&] { server->destroy(); })
           < timedelta::milliseconds(200));
    assert(timedelta::time([&] {
                q->destroy(rpcservice2::acquirestxlock(io)); })
        < timedelta::milliseconds(100)); }

void
tests::_eqtest() {
    testcaseIO("eq", "basic", [] (clientio io) {
            eqtestcase(
                io,
                [] (clientio _io,
                    eqclient<unsigned> &c,
                    eventqueue<unsigned> &q) {
                    assert(c.pop() == Nothing);
                    q.queue(52, rpcservice2::acquirestxlock(_io));
                    assert(timedelta::time([&] { assert(c.pop(_io) == 52); })
                           < timedelta::milliseconds(200));
                    assert(c.pop() == Nothing); }); });
    testcaseIO("eq", "overflow", [] (clientio io) {
            auto qconf(eventqueueconfig::dflt());
            qconf.queuelimit = 2;
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
                    assert(timedelta::time([&] {
                                assert(c.pop(_io) == error::eventsdropped); })
                           < timedelta::milliseconds(200));
                    assert(c.pop().just() == error::eventsdropped); },
                qconf); });
    testcaseIO("eq", "multiclient", [] (clientio io) {
            auto qconf(eventqueueconfig::dflt());
            qconf.queuelimit = 2;
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
            auto q(server->mkqueue(proto::eq::names::testunsigned, qconf));
            auto cconfig1(eqclientconfig::dflt());
            cconfig1.get = timedelta::milliseconds(100);
            cconfig1.wait = timedelta::milliseconds(100);
            auto c1(eqclient<unsigned>::connect(
                        io,
                        *pool,
                        sn,
                        proto::eq::names::testunsigned,
                        timedelta::seconds(1).future(),
                        cconfig1)
                    .fatal("connecting eqclient"));
            auto cconfig2(cconfig1);
            cconfig2.maxqueue = 2;
            auto c2(eqclient<unsigned>::connect(
                        io,
                        *pool,
                        sn,
                        proto::eq::names::testunsigned,
                        timedelta::seconds(1).future(),
                        cconfig2)
                    .fatal("connecting eqclient"));
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
            c2->destroy(io);
            c2 = eqclient<unsigned>::connect(
                io,
                *pool,
                sn,
                proto::eq::names::testunsigned,
                timedelta::seconds(1).future())
                .fatal("connecting eqclient");
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
            c1->destroy(io);
            c2->destroy(io);
            pool->destroy(); });
    testcaseIO("eq", "badconn", [] (clientio io) {
            clustername cn((quickcheck()));
            slavename sn((quickcheck()));
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
            assert(t < timedelta::milliseconds(200)); }); }
