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
#include "wireproto.tmpl"

void
tests::beacon() {
    auto mastername(peername::local("testname"));
    auto port(peername::port((unsigned short)(random() % 32768 + 32768)));
    auto mkbeacon([mastername, port] (
                      const mastersecret &ms,
                      const registrationsecret &rs,
                      controlserver *cs) {
            auto server(beaconserver::build(
                            beaconserverconfig(rs, mastername, ms)
                            .port(port),
                            cs));
            assert(server.issuccess());
            return server.success(); });
    testcaseCS(
        "beacon", "basicconn",
        [mkbeacon, mastername, port]
        (controlserver *cs) {
            auto rs(registrationsecret::mk("rs").just());
            mastersecret ms("ms");
            auto server(mkbeacon(ms, rs, cs));
            auto client(beaconclient(beaconclientconfig(rs)
                                     .port(port)
                                     .retrylimit(maybe<int>(5))));
            assert(client.issuccess());
            assert(client.success().mastername == mastername);
            assert(client.success().secret == rs);
            assert(ms.noncevalid(client.success().nonce,
                                 client.success().connectingname));
            server->status();
            server->destroy(clientio::CLIENTIO); });

    testcaseV("beacon", "noserver", [port] () {
            auto rs(registrationsecret::mk("rs").just());
            auto client(beaconclient(
                            beaconclientconfig(rs)
                            .port(port)
                            .retrylimit(maybe<int>(5))
                            .retryinterval(timedelta::milliseconds(10))));
            assert(client.isfailure());
            assert(client.failure() == error::timeout); });

    testcaseCS("beacon", "badserver", [mastername, port] (controlserver *cs) {
            auto rs(registrationsecret::mk("rs").just());
            mastersecret ms("ms");
            auto server(beaconserver::build(
                            beaconserverconfig(rs, mastername, ms)
                            .port(peername::port(1)),
                            cs));
            assert(server.isfailure());});

    testcaseCS("beacon", "ratelimit", [mastername, port] (controlserver *cs) {
            auto rs(registrationsecret::mk("rs").just());
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
                                     client.success().connectingname));
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
                            pipe.write.dup2(args.first()).fatal("dup2");
                            pipe.write.close();
                            piperead = pipe.read; });
                    auto r(beaconclient(
                               beaconclientconfig(registrationsecret::mk("rs").just())
                               .port(peername::port(
                                         (unsigned short)
                                         (random()%32768 + 32768)))));
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
                       beaconclientconfig(registrationsecret::mk("rs").just())
                       .port(peername::port(
                                 (unsigned short)(random()%32768 + 32768)))));
            assert(r.failure() == error::from_errno(EBADF));});

    /* Send crap to a given UDP socket */
    auto spamfd([] (udpsocket fd) {
            auto r(udpsocket::client().fatal("udpclient"));
            ::buffer buf;
            char b[1000];
            memset(b, 0xff, sizeof(b));
            buf.queue(b, sizeof(b));
            r.send(buf, fd.localname()).fatal("send");
            r.close(); });

    testcaseV("beacon", "clientreceivenodecode", [spamfd] () {
            bool logged = false;
            eventwaiter<loglevel> l(
                logmsg,
                [&logged] (loglevel ll) {
                    if (ll >= loglevel::failure) logged = true; });
            int cntr = 0;
            eventwaiter<pair<udpsocket,nonce> > wait(
                beaconclientreceiving,
                [&spamfd, &cntr] (pair<udpsocket,nonce> args) {
                    if (cntr++ > 1) return;
                    spamfd(args.first()); });
            auto r(beaconclient(
                       beaconclientconfig(registrationsecret::mk("rs").just())
                       .port(peername::port(
                                 (unsigned short)(random()%32768 + 32768)))
                       .retrylimit(1)
                       .retryinterval(timedelta::milliseconds(10))));
            assert(r.isfailure());
            assert(logged);
            assert(cntr < 10); });

    auto sendfdmessage(
        [] (udpsocket fd, const wireproto::tx_message &msg) {
            ::buffer buf;
            msg.serialise(buf);
            auto r(udpsocket::client().fatal("udpclient"));
            r.send(buf, fd.localname()).fatal("send");
            r.close(); });
    testcaseV("beacon", "clientreceivemissingparam", [sendfdmessage] () {
            int cntr =0 ;
            eventwaiter<pair<udpsocket,nonce> > wait(
                beaconclientreceiving,
                [&sendfdmessage, &cntr] (pair<udpsocket,nonce> args) {
                    if (cntr++ > 0) return;
                    sendfdmessage(
                        args.first(),
                        wireproto::tx_message(proto::HAIL::tag)); });
            auto r(beaconclient(
                       beaconclientconfig(registrationsecret::mk("rs").just())
                       .port(peername::port(
                                 (unsigned short)(random()%32768 + 32768)))
                       .retrylimit(1)
                       .retryinterval(timedelta::milliseconds(10))));
            assert(r.isfailure()); });
    testcaseV("beacon", "clientreceivebadparam",
              [sendfdmessage] () {
            /* Arrange for first message received to have a bad
               version number, the second to have a bad digest, and
               the thir to be valid, and make sure that the final
               result is valid. */
            int iter;
            iter = 0;
            registrationsecret rs(registrationsecret::mk("rs").just());
            masternonce mnonce(digest(fields::mk(random())));
            eventwaiter<pair<udpsocket,nonce> > wait(
                beaconclientreceiving,
                [&sendfdmessage, &iter, &rs, &mnonce]
                (pair<udpsocket,nonce> args) {
                    assert(iter == 0 || iter == 1 || iter == 2);
                    peername mastername2(peername::local(
                                             iter == 2
                                             ? "GOOD1"
                                             : "BAD"));
                    sendfdmessage(
                        args.first(),
                        wireproto::tx_message(proto::HAIL::tag)
                        .addparam(proto::HAIL::resp::version,
                                  iter == 0 ? 2u : 1u)
                        .addparam(proto::HAIL::resp::mastername, mastername2)
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
                                           fields::mk(mastername2) +
                                           fields::mk(args.second()) +
                                           fields::mk(rs))));
                    iter++; });
            auto r(beaconclient(
                       beaconclientconfig(registrationsecret::mk("rs").just())
                       .port(peername::port(
                                 (unsigned short)(random()%32768 + 32768)))
                       .retrylimit(3)
                       .retryinterval(timedelta::milliseconds(200))));
            assert(r.issuccess());
            assert(iter == 3);
            assert(r.success().nonce == mnonce);
            assert(r.success().connectingname == peername::local("GOOD2"));
            assert(r.success().mastername == peername::local("GOOD1"));
            assert(r.success().secret == rs); });

    testcaseCS(
        "beacon", "serverreceiveerror",
        [spamfd, sendfdmessage, mkbeacon]
        (controlserver *cs) {
            bool done = false;
            publisher pub;
            subscriber sub;
            subscription ss(sub, pub);
            eventwaiter<udpsocket> wait(
                beaconserverreceive,
                [&spamfd, &done, &pub]
                (udpsocket sock) {
                if (done) return;
                spamfd(sock);
                sock.close();
                done = true;
                pub.publish(); });
            auto server(mkbeacon(mastersecret("ms"),
                                 registrationsecret::mk("rs").just(),
                                 cs));
            while (!done) sub.wait(clientio::CLIENTIO);
            /* make sure race with shutdown goes the right way. */
            (timestamp::now() + timedelta::milliseconds(10)).sleep();
            server->destroy(clientio::CLIENTIO); });

    testcaseCS(
        "beacon", "serverreceivenodecode",
        [spamfd, mkbeacon]
        (controlserver *cs) {
            bool done = false;
            publisher pub;
            subscriber sub;
            subscription ss(sub, pub);
            eventwaiter<udpsocket> wait(
                beaconserverreceive,
                [&spamfd, &done, &pub]
                (udpsocket sock) {
                    spamfd(sock);
                    done = true;
                    pub.publish(); });
            auto server(mkbeacon(mastersecret("ms"),
                                 registrationsecret::mk("rs").just(),
                                 cs));
            while (!done) sub.wait(clientio::CLIENTIO);
            server->destroy(clientio::CLIENTIO); });

    testcaseCS(
        "beacon", "serverreceivebad",
        [sendfdmessage, mkbeacon]
        (controlserver *cs) {
            int cntr = 0;
            publisher pub;
            subscriber sub;
            subscription ss(sub, pub);
            eventwaiter<udpsocket> wait(
                beaconserverreceive,
                [&sendfdmessage, &pub, &cntr]
                (udpsocket sock) {
                    switch (cntr) {
                    case 0:
                        sendfdmessage(
                            sock,
                            wireproto::tx_message(wireproto::msgtag(73)));
                        break;
                    case 1:
                        sendfdmessage(
                            sock,
                            wireproto::tx_message(proto::HAIL::tag));
                        break;
                    case 2:
                        sendfdmessage(
                            sock,
                            wireproto::tx_message(proto::HAIL::tag)
                            .addparam(proto::HAIL::req::version, 1u));
                        break;
                    case 3:
                        sendfdmessage(
                            sock,
                            wireproto::tx_message(proto::HAIL::tag)
                            .addparam(proto::HAIL::req::version, 1u)
                            .addparam(proto::HAIL::req::nonce, nonce::mk()));
                        break;
                    case 4:
                        sendfdmessage(
                            sock,
                            wireproto::tx_message(proto::HAIL::tag)
                            .addparam(proto::HAIL::req::version, 2u)
                            .addparam(proto::HAIL::req::nonce, nonce::mk()));
                        break;
                    }
                    cntr++;
                    if (cntr == 5) pub.publish(); });
            auto server(mkbeacon(mastersecret("ms"),
                                 registrationsecret::mk("rs").just(),
                                 cs));
            while (cntr < 5) sub.wait(clientio::CLIENTIO);
            wireproto::tx_message txm(wireproto::msgtag(5));
            server->statusiface_.getstatus(&txm);
            server->destroy(clientio::CLIENTIO); });
#endif /* TESTING */

    testcaseV("beacon", "statusfuzz", [] () {
            wireproto::roundtrip<beaconserver::status_t>(); });
    testcaseV("beacon", "statusprint", [] () {
            fields::print(
                fields::mk(beaconserver::status_t(quickcheck()))+"\n"); });
}
