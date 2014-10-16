#include "beacontest.H"

#include "beaconclient.H"
#include "beaconserver.H"
#include "test.H"

#include "parsers.tmpl"

void
tests::beacon() {
    /* Basic beacon functionality: can a client find a server, in the
     * simple case? */
    testcaseIO("beacon", "basic", [] (clientio io) {
            clustername cluster((quickcheck()));
            slavename slave((quickcheck()));
            peername::port port((quickcheck()));
            auto s(beaconserver::build(
                       beaconserverconfig::dflt(cluster, slave),
                       actortype::test,
                       port)
                   .fatal("starting beacon server"));
            auto c(beaconclient::build(beaconclientconfig(cluster,
                                                          actortype::test,
                                                          slave))
                   .fatal("starting beacon client"));
            auto r(c->query(io, slave));
            assert(r.type == actortype::test);
            assert(r.name.getport() == port);
            c->destroy(io);
            s->destroy(io); });
    testcaseV("beacon", "clientconfig", [] {
            parsers::roundtrip(parsers::__beaconclientconfig()); });
    testcaseV("beacon", "badconfig", [] {
            beaconclientconfig dflt(clustername::mk("foo").fatal("bad"));
            assert(beaconclientconfig::mk(
                       dflt.cluster(),
                       dflt.type(),
                       dflt.name(),
                       dflt.proto(),
                       timedelta::seconds(-1)) == error::range);
            assert(beaconclientconfig::mk(
                       dflt.cluster(),
                       dflt.type(),
                       dflt.name(),
                       dflt.proto(),
                       dflt.queryinterval(),
                       timedelta::seconds(-1)) == error::range); });
    testcaseIO("beacon", "status", [] (clientio io) {
            /* Not a great test: just make sure that it doesn't crash. */
            clustername cluster((quickcheck()));
            slavename slave((quickcheck()));
            peername::port port((quickcheck()));
            auto c(beaconclient::build(beaconclientconfig(cluster,
                                                          actortype::test,
                                                          slave))
                   .fatal("starting beacon client"));
            c->status();
            auto s(beaconserver::build(
                       beaconserverconfig::dflt(cluster, slave),
                       actortype::test,
                       port)
                   .fatal("starting beacon server"));
            c->status();
            s->status();
            (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
            c->status();
            s->status();
            s->destroy(io);
            c->destroy(io); }); }
