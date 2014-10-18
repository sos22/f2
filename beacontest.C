#include "beacontest.H"

#include "beaconclient.H"
#include "beaconserver.H"
#include "logging.H"
#include "proto.H"
#include "rpcclient.H"
#include "test.H"
#include "udpsocket.H"

#include "parsers.tmpl"
#include "test.tmpl"

void
tests::beacon() {
    testcaseIO("beacon", "basic", [] (clientio io) {
            /* Basic beacon functionality: can a client find a server,
             * in the simple case? */
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
    testcaseIO("beacon", "dropserver", [] (clientio io) {
            /* Do dead servers drop out of the client cache reasonably
             * promptly? */
            clustername cluster((quickcheck()));
            slavename slave((quickcheck()));
            peername::port port((quickcheck()));
            auto s(beaconserver::build(
                       beaconserverconfig(beaconconfig::dflt,
                                          cluster,
                                          slave,
                                          timedelta::seconds(1)),
                       actortype::test,
                       port)
                   .fatal("starting beacon server"));
            auto c(beaconclient::build(
                       beaconclientconfig::mk(cluster,
                                              actortype::test,
                                              slave,
                                              beaconconfig::dflt,
                                              timedelta::milliseconds(100))
                       .fatal("building beacon client config"))
                   .fatal("starting beacon client"));
            /* Wait for client to discover server. */
            c->query(io, slave);
            assert(c->poll(slave) != Nothing);
            /* Shut down server. */
            s->destroy(io);
            /* Should drop out within the beaconserver liveness time,
             * plus a bit of fuzz, but not too much sooner. */
            auto destroyed(timestamp::now());
            (destroyed + timedelta::seconds(1) - timedelta::milliseconds(100))
                .sleep(io);
            assert(c->poll(slave) != Nothing);
            (destroyed + timedelta::seconds(1) + timedelta::milliseconds(100))
                .sleep(io);
            assert(c->poll(slave) == Nothing);
            /* Should come back almost immediately when we re-create
             * the server. */
            s = beaconserver::build(
                beaconserverconfig(beaconconfig::dflt,
                                   cluster,
                                   slave,
                                   timedelta::seconds(1)),
                actortype::test,
                port)
                .fatal("restarting beacon server");
            assert(timedelta::time([c, io, port, &slave] {
                        assert(c->query(io, slave).name.getport() == port); })
                < timedelta::milliseconds(100));
            s->destroy(io);
            c->destroy(io); });
    testcaseIO("beacon", "clientfilter", [] (clientio io) {
            /* Does filtering by type work? */
            clustername cluster((quickcheck()));
            slavename slave((quickcheck()));
            peername::port port((quickcheck()));
            auto s(beaconserver::build(
                       beaconserverconfig::dflt(cluster,
                                                slave),
                       actortype::test,
                       port)
                   .fatal("starting beacon server"));
            auto c(beaconclient::build(beaconclientconfig(
                                           cluster,
                                           actortype::storageslave))
                   .fatal("starting beacon client"));
            assert(c->poll(slave) == Nothing);
            (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
            assert(c->poll(slave) == Nothing);
            peername::port port2((quickcheck()));
            auto s2(beaconserver::build(
                        beaconserverconfig::dflt(cluster,
                                                 slave),
                        actortype::storageslave,
                        port2)
                    .fatal("starting second beacon server"));
            assert(c->query(io, slave).name.getport() == port2);
            c->destroy(io);
            s2->destroy(io);
            s->destroy(io); });
    testcaseIO("beacon", "iterator", [] (clientio io) {
            clustername cluster((quickcheck()));
            slavename slave1((quickcheck()));
            peername::port port1((quickcheck()));
            slavename slave2((quickcheck()));
            peername::port port2((quickcheck()));
            slavename slave3((quickcheck()));
            peername::port port3((quickcheck()));
            auto c(beaconclient::build(cluster)
                   .fatal("starting beacon client"));
            assert(c->start().finished());
            auto s1(beaconserver::build(
                        beaconserverconfig::dflt(cluster,
                                                 slave1),
                        actortype::test,
                        port1)
                    .fatal("starting beacon server"));
            auto s2(beaconserver::build(
                        beaconserverconfig::dflt(cluster,
                                                 slave2),
                        actortype::storageslave,
                        port2)
                    .fatal("starting beacon server"));
            auto s3(beaconserver::build(
                        beaconserverconfig::dflt(cluster,
                                                 slave3),
                        actortype::master,
                        port3)
                    .fatal("starting beacon server"));
            assert(c->query(io, slave1).name.getport() == port1);
            assert(c->query(io, slave2).name.getport() == port2);
            assert(c->query(io, slave3).name.getport() == port3);
            bool found1 = false;
            bool found2 = false;
            bool found3 = false;
            for (auto it(c->start()); !it.finished(); it.next()) {
                if (it.name() == slave1) {
                    assert(!found1);
                    assert(it.type() == actortype::test);
                    assert(it.peer().getport() == port1);
                    found1 = true; }
                else if (it.name() == slave2) {
                    assert(!found2);
                    assert(it.type() == actortype::storageslave);
                    assert(it.peer().getport() == port2);
                    found2 = true; }
                else if (it.name() == slave3) {
                    assert(!found3);
                    assert(it.type() == actortype::master);
                    assert(it.peer().getport() == port3);
                    found3 = true; }
                else abort(); }
            assert(found1);
            assert(found2);
            assert(found3);
            {   auto it(c->start(actortype::master));
                assert(!it.finished());
                assert(it.type() == actortype::master);
                assert(it.peer().getport() == port3);
                it.next();
                assert(it.finished()); }
            c->destroy(io);
            s1->destroy(io);
            s2->destroy(io);
            s3->destroy(io); });
    testcaseV("beacon", "clientconfig", [] {
            parsers::roundtrip(parsers::__beaconclientconfig()); });
    testcaseV("beacon", "serverconfig", [] {
            parsers::roundtrip(parsers::__beaconserverconfig()); });
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
    testcaseCS("beacon", "status", [] (controlserver *cs, clientio io) {
            /* Not a great test: just make sure that it doesn't crash. */
            clustername cluster((quickcheck()));
            slavename slave((quickcheck()));
            peername::port port((quickcheck()));
            auto c(beaconclient::build(beaconclientconfig(cluster,
                                                          actortype::test,
                                                          slave),
                                       cs)
                   .fatal("starting beacon client"));
            c->status();
            auto s(beaconserver::build(
                       beaconserverconfig::dflt(cluster, slave),
                       actortype::test,
                       port,
                       cs)
                   .fatal("starting beacon server"));
            c->status();
            s->status();
            (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
            c->status();
            s->status();
            auto cc(rpcclient::connect(io, cs->localname())
                    .fatal("connecting to control server"));
            delete cc->call(io, wireproto::req_message(proto::STATUS::tag))
                .fatal("STATUS call");
            delete cc;
            s->destroy(io);
            c->destroy(io); });
    testcaseIO("beacon", "clientfailure1", [] (clientio) {
            hook<orerror<udpsocket>, udpsocket> h(
                udpsocket::_client, [] (udpsocket u) {
                    u.close();
                    return error::pastend; });
            assert(beaconclient::build(
                       beaconclientconfig(clustername(quickcheck())))
                   == error::pastend); });
    testcaseIO("beacon", "clientfailure2", [] (clientio io) {
            unsigned errcount = 0;
            beaconclient *c;
            hook<orerror<void>, const udpsocket &> h(
                udpsocket::_receive,
                [&c, &errcount] (const udpsocket &s) -> orerror<void> {
                    if (c == NULL || s != c->listenfd) return Success;
                    if (errcount++ < 3) return error::pastend;
                    else return Success; });
            clustername cluster((quickcheck()));
            slavename slave((quickcheck()));
            peername::port port((quickcheck()));
            auto s(beaconserver::build(
                       beaconserverconfig::dflt(cluster, slave),
                       actortype::test,
                       port)
                   .fatal("starting beacon server"));
            c = beaconclient::build(beaconclientconfig(cluster))
                .fatal("contructing beacon");
            auto tv(timedelta::time(
                        [c, port, &slave] {
                            assert(c->query(clientio::CLIENTIO, slave)
                                   .name
                                   .getport() == port); }));
            assert(errcount >= 3);
            assert(tv >= timedelta::milliseconds(200));
            assert(tv <= timedelta::milliseconds(400));
            s->destroy(io);
            c->destroy(io); });
    testcaseIO("beacon", "clientbroadcastfailure", [] (clientio io) {
            /* Client should be able to recover if its first few
             * broadcasts fail. */
            /* Start server first so that the server broadcast doesn't
             * save the client. */
            clustername cluster((quickcheck()));
            slavename slave((quickcheck()));
            peername::port port((quickcheck()));
            auto s(beaconserver::build(beaconserverconfig::dflt(cluster, slave),
                                       actortype::test,
                                       port)
                   .fatal("starting beacon server"));
            unsigned cntr = 0;
            hook<orerror<void>, const udpsocket &, const peername &> h(
                udpsocket::_send,
                [&cntr]
                (const udpsocket &, const peername &p) -> orerror<void> {
                    if (!p.isbroadcast()) return Success;
                    if (cntr++ > 3) return Success;
                    return error::pastend; });
            auto c(beaconclient::build(
                       beaconclientconfig::mk(
                           cluster,
                           Nothing,
                           Nothing,
                           beaconconfig::dflt,
                           timedelta::seconds(1),
                           timedelta::milliseconds(100))
                       .fatal("beaconclientconfig::mk"))
                   .fatal("beaconclient::build"));
            auto tv(timedelta::time([c, &slave, port, io] {
                        assert(c->query(io, slave).name.getport() == port); }));
            assert(cntr >= 3);
            /* First three broadcasts fail, and we do one every 100ms,
             * so it should take at least 200ms to complete
             * (fencepost). */
            assert(tv >= timedelta::milliseconds(200));
            /* Should complete fairly quickly after that. */
            assert(tv <= timedelta::milliseconds(400));
            s->destroy(io);
            c->destroy(io); });
    testcaseIO("beacon", "clientdirectfailure", [] (clientio io) {
            /* If we stop the client from doing directed calls then
             * things should drop out of the cache at the expiry time
             * and come back at the broadcast time. */
            clustername cluster((quickcheck()));
            slavename slave((quickcheck()));
            peername::port port((quickcheck()));
            auto s(beaconserver::build(
                       beaconserverconfig(beaconconfig::dflt,
                                          cluster,
                                          slave,
                                          timedelta::milliseconds(500)),
                       actortype::test,
                       port)
                   .fatal("beaconserver::build"));
            auto c(beaconclient::build(beaconclientconfig::mk(
                                           cluster,
                                           Nothing,
                                           Nothing,
                                           beaconconfig::dflt,
                                           timedelta::milliseconds(100),
                                           timedelta::seconds(1))
                                       .fatal("beaconclientconfig::mk"))
                   .fatal("beaconclient::build"));
            c->query(io, slave);
            bool blocksends = true;
            unsigned cntr = 0;
            hook<orerror<void>, const udpsocket &, const peername &> h(
                udpsocket::_send,
                [&blocksends, c, &cntr]
                (const udpsocket &sock, const peername &p) -> orerror<void> {
                    if (sock != c->listenfd && sock != c->clientfd) {
                        return Success; }
                    else if (p.isbroadcast()) return Success;
                    else if (!blocksends) return Success;
                    cntr++;
                    return error::pastend; });
            /* Poll to see when it drops out. */
            auto start(timestamp::now());
            while (c->poll(slave) != Nothing) {
                (timestamp::now() + timedelta::milliseconds(10)).sleep(io); }
            auto drop(timestamp::now());
            /* Should be near the expiry time. */
            assert(drop - start >= timedelta::milliseconds(400));
            assert(drop - start <= timedelta::milliseconds(600));
            /* Should have made about 5 attempts. */
            assert(cntr >= 3);
            assert(cntr <= 7);
            /* Poll to see when it comes back. */
            while (c->poll(slave) == Nothing) {
                (timestamp::now() + timedelta::milliseconds(10)).sleep(io); }
            auto recover(timestamp::now());
            assert(recover - start >= timedelta::milliseconds(900));
            assert(recover - start <= timedelta::milliseconds(1100));
            c->destroy(io);
            s->destroy(io); });
    testcaseIO("beacon", "clientspam", [] (clientio io) {
            /* Send crap over the client's listening port and make
             * sure it (a) generates low-level logging messages, (b)
             * doesn't generate any high-level ones, and (c) can
             * recover when we stop. */
            beaconconfig ports((quickcheck()));
            clustername cluster((quickcheck()));
            auto c(beaconclient::build(
                       beaconclientconfig(cluster,
                                          Nothing,
                                          Nothing,
                                          ports))
                   .fatal("creating beacon client"));
            unsigned nrlow = 0;
            unsigned nrhigh = 0;
            eventwaiter< ::loglevel> waiter(
                tests::logmsg,
                [&nrlow, &nrhigh] (loglevel level) {
                    if (level >= loglevel::info) nrhigh++;
                    else nrlow++; });
            auto sock(udpsocket::client()
                      .fatal("UDP socket"));
            auto sendto(peername::loopback(ports.respport));
            for (unsigned x = 0; x < 100; x++) {
                ::buffer b;
                unsigned len((unsigned)(random() % 1500 + 1));
                unsigned char buf[len];
                for (unsigned y = 0; y < len; y++) {
                    buf[y] = (unsigned char)random(); }
                b.queue(buf, len);
                sock.send(b, sendto).fatal("sending spam"); }
            assert(nrlow != 0);
            assert(nrhigh == 0);
            slavename name((quickcheck()));
            peername::port port((quickcheck()));
            auto s(beaconserver::build(
                       beaconserverconfig(
                           ports,
                           cluster,
                           name,
                           timedelta::seconds(120)),
                       actortype::test,
                       port)
                   .fatal("creating server"));
            assert(c->query(io, name).name.getport() == port);
            s->destroy(io);
            c->destroy(io); });
}
