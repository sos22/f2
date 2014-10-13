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
            auto c(beaconclient::build(
                       beaconclientconfig::dflt(cluster,
                                                actortype::test,
                                                slave))
                   .fatal("starting beacon client"));
            auto r(c->query(io, slave));
            assert(r.type == actortype::test);
            assert(r.server.samehost(r.beacon));
            assert(r.server.getport() == port);
            c->destroy(io);
            s->destroy(io); });
    testcaseV("beacon", "clientconfig", [] {
            parsers::roundtrip(parsers::__beaconclientconfig()); });
}
