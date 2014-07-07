#include "beacontest.H"

#include "beaconclient.H"
#include "beaconserver.H"
#include "controlserver.H"
#include "fields.H"
#include "logging.H"
#include "mastersecret.H"
#include "nonce.H"
#include "pair.H"
#include "peername.H"
#include "proto.H"
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

#if TESTING
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
            doit(false); });

    testcaseV("beacon", "clientreceivefailed", [] () {
            eventwaiter<pair<udpsocket,nonce> > wait(
                beaconclientreceiving,
                [] (pair<udpsocket,nonce> args) {
                    args.first().asfd().close(); });
            auto r(beaconclient(
                       beaconclientconfig(registrationsecret::mk("rs"))
                       .port(peername::port(random()%32768 + 32768))));
            assert(r.failure() == error::from_errno(EBADF));});

    testcaseV("beacon", "clientreceivenodecode", [] () {
            bool logged = false;
            eventwaiter<loglevel> l(
                logmsg,
                [&logged] (loglevel ll) {
                    if (ll >= loglevel::failure) logged = true; });
            int cntr = 0;
            eventwaiter<pair<udpsocket,nonce> > wait(
                beaconclientreceiving,
                [&cntr] (pair<udpsocket,nonce> args) {
                    /* Arrange that the client reads an invalid
                     * wire message. */
                    if (cntr++ > 1) return;
                    auto r(udpsocket::client());
                    ::buffer buf;
                    char b[1000];
                    memset(b, 0xff, sizeof(b));
                    buf.queue(b, sizeof(b));
                    auto rr(r.success().send(buf, args.first().localname()));
                    assert(rr == Nothing);
                    r.success().close(); });
            auto r(beaconclient(
                       beaconclientconfig(registrationsecret::mk("rs"))
                       .port(peername::port(random()%32768 + 32768))
                       .retrylimit(1)
                       .retryinterval(timedelta::milliseconds(10))));
            assert(r.isfailure());
            assert(logged);
            assert(cntr < 10); });

    auto sendfdmessage(
        [] (udpsocket fd, const wireproto::tx_message &msg) {
            ::buffer buf;
            msg.serialise(buf);
            auto r(udpsocket::client());
            auto rr(r.success().send(buf, fd.localname()));
            assert(rr == Nothing);
            r.success().close(); });
    testcaseV("beacon", "clientreceivemissingparam", [&sendfdmessage] () {
            int cntr =0 ;
            eventwaiter<pair<udpsocket,nonce> > wait(
                beaconclientreceiving,
                [&sendfdmessage, &cntr] (pair<udpsocket,nonce> args) {
                    if (cntr++ > 0) return;
                    sendfdmessage(
                        args.first(),
                        wireproto::tx_message(proto::HAIL::tag)); });
            auto r(beaconclient(
                       beaconclientconfig(registrationsecret::mk("rs"))
                       .port(peername::port(random()%32768 + 32768))
                       .retrylimit(1)
                       .retryinterval(timedelta::milliseconds(10))));
            assert(r.isfailure()); });
    testcaseV("beacon", "clientreceivebadparam",
              [&sendfdmessage] () {
            /* Arrange for first message received to have a bad
               version number, the second to have a bad digest, and
               the thir to be valid, and make sure that the final
               result is valid. */
            int iter;
            iter = 0;
            registrationsecret rs(registrationsecret::mk("rs"));
            masternonce mnonce(digest(fields::mk(random())));
            eventwaiter<pair<udpsocket,nonce> > wait(
                beaconclientreceiving,
                [&sendfdmessage, &iter, &rs, &mnonce]
                (pair<udpsocket,nonce> args) {
                    assert(iter == 0 || iter == 1 || iter == 2);
                    peername mastername(peername::local(
                                            iter == 2
                                            ? "GOOD1"
                                            : "BAD"));
                    sendfdmessage(
                        args.first(),
                        wireproto::tx_message(proto::HAIL::tag)
                        .addparam(proto::HAIL::resp::version,
                                  iter == 0 ? 2u : 1u)
                        .addparam(proto::HAIL::resp::mastername, mastername)
                        .addparam(proto::HAIL::resp::slavename,
                                  iter == 2
                                  ? peername::local("GOOD2")
                                  : peername::local("BAD"))
                        .addparam(proto::HAIL::resp::nonce,
                                  iter == 2
                                  ? mnonce
                                  : masternonce(digest(fields::mk(random()))))
                        .addparam(proto::HAIL::resp::digest,
                                  iter == 1
                                  ? digest(fields::mk(random()))
                                  : digest("A" +
                                           fields::mk(mastername) +
                                           fields::mk(args.second()) +
                                           fields::mk(rs))));
                    iter++; });
            auto r(beaconclient(
                       beaconclientconfig(registrationsecret::mk("rs"))
                       .port(peername::port(random()%32768 + 32768))
                       .retrylimit(3)
                       .retryinterval(timedelta::milliseconds(10))));
            assert(r.issuccess());
            assert(iter == 3);
            assert(r.success().nonce == mnonce);
            assert(r.success().slavename == peername::local("GOOD2"));
            assert(r.success().mastername == peername::local("GOOD1"));
            assert(r.success().secret == rs); });
#endif /* TESTING */
}
