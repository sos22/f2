#include "beacontest.H"

#include "beaconclient.H"
#include "beaconserver.H"
#include "controlserver.H"
#include "fields.H"
#include "mastersecret.H"
#include "peername.H"
#include "pubsub.H"
#include "registrationsecret.H"
#include "test.H"

void
tests::beacon() {
    testcaseCS("beacon", "basicconn", [] (controlserver *cs) {
            auto port(peername::port(random() % 32768 + 32768));
            auto mastername(peername::local("testname"));
            auto rs(registrationsecret::mk("rs"));
            mastersecret ms("ms");
            auto server(beaconserver::build(
                            beaconserverconfig(rs, mastername, ms)
                            .port(port),
                            cs));
            assert(server.issuccess());
            auto client(beaconclient(beaconclientconfig(rs)
                                     .port(port)
                                     .retrylimit(maybe<int>(5))));
            assert(client.issuccess());
            assert(client.success().mastername == mastername);
            assert(client.success().secret == rs);
            assert(ms.noncevalid(client.success().nonce,
                                 client.success().slavename));
            server.success()->destroy(clientio::CLIENTIO); });

    testcaseV("beacon", "noserver", [] () {
            auto port(peername::port(random() % 32768 + 32768));
            auto rs(registrationsecret::mk("rs"));
            auto client(beaconclient(
                            beaconclientconfig(rs)
                            .port(port)
                            .retrylimit(maybe<int>(5))
                            .retryinterval(timedelta::milliseconds(10))));
            assert(client.isfailure());
            assert(client.failure() == error::timeout); });

    testcaseCS("beacon", "badserver", [] (controlserver *cs) {
            auto port(peername::port(1)); /* Any port we can't listen on */
            auto mastername(peername::local("testname"));
            auto rs(registrationsecret::mk("rs"));
            mastersecret ms("ms");
            auto server(beaconserver::build(
                            beaconserverconfig(rs, mastername, ms)
                            .port(port),
                            cs));
            assert(server.isfailure());});

    testcaseCS("beacon", "ratelimit", [] (controlserver *cs) {
            auto port(peername::port(random() % 32768 + 32768));
            auto mastername(peername::local("testname"));
            auto rs(registrationsecret::mk("rs"));
            mastersecret ms("ms");
            auto server(beaconserver::build(
                            beaconserverconfig(rs, mastername, ms)
                            .maxresponses(frequency::hz(100))
                            .port(port),
                            cs));
            assert(server.issuccess());
            /* Should be able to run the protocol about 300 times in
             * two seconds (rate limiter starts with 100 and
             * replenishes at 100Hz). */
            timestamp deadline(timestamp::now() + timedelta::seconds(2));
            int cntr = 0;
            while (timestamp::now() < deadline) {
                auto client(beaconclient(beaconclientconfig(rs)
                                         .port(port)
                                         .retrylimit(maybe<int>(2))
                                         .retryinterval(timedelta::milliseconds(20))));
                assert(client.issuccess());
                assert(client.success().mastername == mastername);
                assert(client.success().secret == rs);
                assert(ms.noncevalid(client.success().nonce,
                                     client.success().slavename));
                cntr++; }
            /* Allow a little bit of fuzz because of timing effects. */
            assert(cntr >= 290);
            assert(cntr <= 310);
            server.success()->destroy(clientio::CLIENTIO); });

}
