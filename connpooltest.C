#include "connpool.H"

#include "beaconclient.H"
#include "error.H"
#include "logging.H"
#include "quickcheck.H"
#include "rpcservice2.H"
#include "serialise.H"
#include "test.H"

#include "connpool.tmpl"
#include "rpcservice2.tmpl"
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
            initlogging("T");
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
                []
                (deserialise1 &, connpool::connlock) -> orerror<char *> {
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
                    (deserialise1 &ds, connpool::connlock) -> orerror<char *>{
                        string msg(ds);
                        unsigned cntr(ds);
                        assert(!ds.isfailure());
                        assert(msg == "boo");
                        assert(cntr == 76);
                        return (char *)7; })
                == (char *)7);
            pool->destroy();
            srv->destroy(io); });
}
