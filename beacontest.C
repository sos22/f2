#include "beacontest.H"

#include "beaconclient.H"
#include "beaconserver.H"
#include "controlserver.H"
#include "fields.H"
#include "logging.H"
#include "mastersecret.H"
#include "pair.H"
#include "peername.H"
#include "pubsub.H"
#include "registrationsecret.H"
#include "test.H"

#include "test.tmpl"

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

    testcaseV("beacon", "clientsendtrunc", [] () {
            auto doit([] (bool send0) {
                    bool generatedlog;
                    generatedlog = false;
                    eventwaiter<loglevel> logwait(
                        tests::logmsg,
                        [&generatedlog] (loglevel level) {
                            if (level >= loglevel::failure) {
                                generatedlog = true; } } );
                    fd_t piperead;
                    eventwaiter< ::pair< ::fd_t, ::buffer *> > wait(
                        tests::beaconclientreadytosend,
                        [&piperead, send0]
                        (::pair< ::fd_t, ::buffer *> args) {
                            /* Replace the socket with a nearly-full pipe
                               O_NONBLOCK pipe to see what happens when the
                               send is truncated. */
                            auto _pipe(fd_t::pipe());
                            auto &pipe(_pipe.success());
                            pipe.write.nonblock(true);
                            pipe.read.nonblock(true);
                            char b[4096];
                            memset(b, 'x', sizeof(b));
                            while (pipe.write.write(clientio::CLIENTIO,
                                                    b,
                                                    sizeof(b)).issuccess())
                                ;
                            while (pipe.write.write(clientio::CLIENTIO,
                                                    b,
                                                    1).issuccess())
                                ;
                            if (!send0) {
                                /* Give it a little bit of space, but
                                   not enough to complete the send. */
                                auto r(pipe.read.read(clientio::CLIENTIO,
                                                      b,
                                                      10));
                                assert(r.success() == 10); }
                            auto r(pipe.write.dup2(args.first()));
                            assert(r == Nothing);
                            pipe.write.close();
                            piperead = pipe.read; });
                    auto r(beaconclient(
                               beaconclientconfig(registrationsecret::mk("rs"))
                               .port(peername::port(random()%32768 + 32768))));
                    assert(r.failure() == error::truncated);
                    assert(generatedlog);
                    piperead.close(); });
            doit(true);
            doit(false); }); }

template class tests::eventwaiter<pair<fd_t, buffer*> >;
template class std::function<void (pair<fd_t, buffer*>)>;
template class tests::event<pair<fd_t, buffer*> >;
