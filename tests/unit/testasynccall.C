/* Simple test of the asynccall machinery. */
#include "asynccall.H"
#include "connpool.H"
#include "test2.H"

#include "tests/lib/testservices.H"

#include "asynccall.tmpl"
#include "orerror.tmpl"
#include "test2.tmpl"

#include "tests/lib/testservices.tmpl"

class slowcallimpl;
struct slowcalldescr {
    typedef slowcallimpl _friend;
    typedef unsigned _resT;
    typedef connpool::asynccallT<unsigned>::token _innerTokenT;
    typedef slowcallimpl _implT; };
typedef asynccall<slowcalldescr> asyncslowcall;
class slowcallimpl {
public: asyncslowcall api;
public: connpool::asynccallT<unsigned> &cl;
public: slowcallimpl(connpool &pool,
                     const agentname &an,
                     timedelta _td,
                     unsigned _key)
    : api(),
      cl(*pool.call<unsigned>(
             an,
             interfacetype::test,
             Nothing,
             [_td, _key] (serialise1 &s, connpool::connlock) {
                 s.push(_td);
                 s.push(_key); },
             [] (deserialise1 &ds, connpool::connlock) -> unsigned {
                 return (unsigned)ds; })) {} };

template <> orerror<unsigned>
asyncslowcall::pop(token t) {
    auto r(impl().cl.pop(t.inner));
    delete &impl();
    return r; }

static testmodule __testasynccall(
    "asynccall",
    list<filename>::mk("asynccall.tmpl", "asynccall.H"),
    "basic", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto &srv(*rpcservice2::listen<slowservice>(
                      io,
                      cn,
                      sn,
                      peername::all(peername::port::any))
                  .fatal("starting service"));
        auto &pool(*connpool::build(cn).fatal("building pool"));
        auto start(timestamp::now());
        auto &c((new slowcallimpl(pool, sn, 100_ms, 5))->api);
        assert(timestamp::now() - start < 10_ms);
        auto t(c.finished());
        assert(t == Nothing);
        {   subscriber sub;
            subscription ss(sub, c.pub());
            t = c.finished();
            while (t == Nothing) {
                sub.wait(io);
                t = c.finished(); } }
        assert(timestamp::now() - start > 100_ms);
        assert(c.pop(t.just()) == 5);
        pool.destroy();
        srv.destroy(io); },
    "convenience", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto &srv(*rpcservice2::listen<slowservice>(
                      io,
                      cn,
                      sn,
                      peername::all(peername::port::any))
                  .fatal("starting service"));
        auto &pool(*connpool::build(cn).fatal("building pool"));
        auto start(timestamp::now());
        auto &c((new slowcallimpl(pool, sn, 100_ms, 5))->api);
        assert(timestamp::now() - start < 10_ms);
        assert(c.pop(io) == 5);
        assert(timestamp::now() - start > 100_ms);
        pool.destroy();
        srv.destroy(io); },
    "abort", [] (clientio io) {
        quickcheck q;
        auto cn(mkrandom<clustername>(q));
        agentname sn(q);
        auto &srv(*rpcservice2::listen<slowservice>(
                      io,
                      cn,
                      sn,
                      peername::all(peername::port::any))
                  .fatal("starting service"));
        auto &pool(*connpool::build(cn).fatal("building pool"));
        auto start(timestamp::now());
        auto &c((new slowcallimpl(pool, sn, 3600_s, 5))->api);
        assert(timestamp::now() - start < 10_ms);
        (50_ms).future().sleep(io);
        assert(srv.nrabandoned == 0);
        c.abort();
        assert(timestamp::now() - start < 60_ms);
        (50_ms).future().sleep(io);
        assert(srv.nrabandoned == 1);
        srv.destroy(io);
        pool.destroy(); });
