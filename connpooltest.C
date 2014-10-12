#include "connpool.H"

#include "beaconclient.H"
#include "error.H"
#include "proto.H"
#include "test.H"

#include "timedelta.tmpl"

void
tests::_connpool() {
    testcaseIO("connpool", "null", [] (clientio io) {
            /* Tests of what happens when there's nothing to connect
             * to. */
            auto bc(beaconclient::build(
                        beaconclientconfig::dflt(
                            clustername::mk("cluster to which noone connects")
                            .fatal("dummy clustername")))
                    .fatal("building beacon client"));
            auto pool(new connpool(bc));
            auto tv(timedelta::time<pooledconnection *>(
                        [pool] {
                            return pool->connect(slavename("nonesuch")); }));
            assert(tv.td < timedelta::milliseconds(100));
            assert(tv.v != NULL);
            auto conn(tv.v);
            auto timeout1(
                timedelta::time(
                    [conn, io] {
                        assert(
                            conn->call(
                                io,
                                wireproto::req_message(proto::PING::tag),
                                timestamp::now() + timedelta::milliseconds(10))
                            == error::timeout); }));
            assert(timeout1 >= timedelta::milliseconds(10));
            assert(timeout1 <= timedelta::milliseconds(110));
            auto slowcall(conn->call(wireproto::req_message(proto::PING::tag)));
            (timestamp::now() + timedelta::milliseconds(10)).sleep(io);
            delete pool;
            (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
            assert(
                timedelta::time(
                    [slowcall] {
                        assert(slowcall->finished().isjust());
                        assert(slowcall->pop(slowcall->finished().just()) ==
                               error::disconnected); })
                <= timedelta::milliseconds(100));
            (timestamp::now() + timedelta::milliseconds(20)).sleep(io);
            conn->put(); }); }
