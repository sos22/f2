#include "connpool.H"

#include "beaconclient.H"
#include "error.H"
#include "logging.H"
#include "quickcheck.H"
#include "test.H"

#include "timedelta.tmpl"

void
tests::_connpool() {
    testcaseIO("connpool", "null", [] (clientio io) {
            initlogging("T");
            /* Tests of what happens when there's nothing to connect
             * to. */
            clustername cn((quickcheck()));
            auto start(timestamp::now());
            auto pool(connpool::build(cn)
                      .fatal("starting conn pool"));
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
            assert(end - start < timedelta::milliseconds(10)); }); }
