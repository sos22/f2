#include "beaconclient.H"
#include "beaconserver.H"
#include "logging.H"
#include "test.H"
#include "test2.H"
#include "udpsocket.H"

#include "parsers.tmpl"
#include "serialise.tmpl"
#include "test.tmpl"
#include "test2.tmpl"

static testmodule __beacontests(
    "beacon",
    list<filename>::mk("beacon.C",
                       "beacon.H",
                       "beaconclient.C",
                       "beaconclient.H",
                       "beaconserver.C",
                       "beaconserver.H"),
    testmodule::BranchCoverage(80_pc),
    "serialise", [] {
        quickcheck q;
        serialise<proto::beacon::req>(q);
        serialise<proto::beacon::resp>(q); },
    "config", [] {
        quickcheck q;
        serialise<beaconconfig>(q);
        serialise<beaconserverconfig>(q); },
    "basic", [] (clientio io) {
        /* Basic beacon functionality: can a client find a server,
         * in the simple case? */
        clustername cluster((quickcheck()));
        agentname agent((quickcheck()));
        peername::port port((quickcheck()));
        auto s(beaconserver::build(
                   beaconserverconfig::dflt(cluster, agent),
                   mklist(interfacetype::test),
                   port)
               .fatal("starting beacon server"));
        auto c(beaconclient::build(beaconclientconfig(cluster,
                                                      interfacetype::test,
                                                      agent))
               .fatal("starting beacon client"));
        auto r(c->query(io, agent));
        assert(r.type.length() == 1);
        assert(r.type.idx(0) == interfacetype::test);
        assert(r.name.getport() == port);
        c->destroy();
        s->destroy(io); },
    "refresh", [] (clientio io) {
        /* Make sure it hangs around past the server-specified
         * expiry time i.e. that refresh works. */
        clustername cluster((quickcheck()));
        agentname agent((quickcheck()));
        peername::port port((quickcheck()));
        auto s(beaconserver::build(
                   beaconserverconfig(
                       beaconconfig::dflt,
                       cluster,
                       agent,
                       timedelta::milliseconds(500)),
                   mklist(interfacetype::test),
                   port)
               .fatal("starting beacon server"));
        auto c(beaconclient::build(
                   beaconclientconfig::mk(cluster,
                                          interfacetype::test,
                                          agent,
                                          beaconconfig::dflt,
                                          timedelta::milliseconds(100),
                                          timedelta::weeks(10))
                   .fatal("beaconclientconfig::mk"))
               .fatal("starting beacon client"));
        auto start(timestamp::now());
        c->query(io, agent);
        while (timestamp::now() < start + timedelta::milliseconds(600)) {
            assert(c->poll(agent) != Nothing); }
        c->destroy();
        s->destroy(io); },
    "dropserver", [] (clientio io) {
        /* Do dead servers drop out of the client cache reasonably
         * promptly? */
        clustername cluster((quickcheck()));
        agentname agent((quickcheck()));
        peername::port port((quickcheck()));
        auto s(beaconserver::build(
                   beaconserverconfig(beaconconfig::dflt,
                                      cluster,
                                      agent,
                                      timedelta::seconds(1)),
                   mklist(interfacetype::test),
                   port)
               .fatal("starting beacon server"));
        auto c(beaconclient::build(
                   beaconclientconfig::mk(cluster,
                                          interfacetype::test,
                                          agent,
                                          beaconconfig::dflt,
                                          timedelta::milliseconds(100))
                   .fatal("building beacon client config"))
               .fatal("starting beacon client"));
        /* Wait for client to discover server. */
        c->query(io, agent);
        assert(c->poll(agent) != Nothing);
        /* Shut down server. */
        s->destroy(io);
        /* Should drop out within the beaconserver liveness time,
         * plus a bit of fuzz, but not too much sooner. */
        auto destroyed(timestamp::now());
        (destroyed + timedelta::seconds(1) - timedelta::milliseconds(100))
            .sleep(io);
        assert(c->poll(agent) != Nothing);
        (destroyed + timedelta::seconds(1) + timedelta::milliseconds(100))
            .sleep(io);
        assert(c->poll(agent) == Nothing);
        /* Should come back almost immediately when we re-create
         * the server. */
        s = beaconserver::build(
            beaconserverconfig(beaconconfig::dflt,
                               cluster,
                               agent,
                               timedelta::seconds(1)),
            mklist(interfacetype::test),
            port)
            .fatal("restarting beacon server");
        assert(timedelta::time([c, io, port, &agent] {
                    assert(c->query(io, agent).name.getport() == port); })
            < timedelta::milliseconds(100));
        s->destroy(io);
        c->destroy(); },
    "clientfilter", [] (clientio io) {
        /* Does filtering by type work? */
        clustername cluster((quickcheck()));
        agentname agent((quickcheck()));
        peername::port port((quickcheck()));
        auto s(beaconserver::build(
                   beaconserverconfig::dflt(cluster,
                                            agent),
                   mklist(interfacetype::test),
                   port)
               .fatal("starting beacon server"));
        auto c(beaconclient::build(beaconclientconfig(
                                       cluster,
                                       interfacetype::storage))
               .fatal("starting beacon client"));
        assert(c->poll(agent) == Nothing);
        (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
        assert(c->poll(agent) == Nothing);
        peername::port port2((quickcheck()));
        auto s2(beaconserver::build(
                    beaconserverconfig::dflt(cluster,
                                             agent),
                    mklist(interfacetype::storage),
                    port2)
                .fatal("starting second beacon server"));
        assert(c->query(io, agent).name.getport() == port2);
        c->destroy();
        s2->destroy(io);
        s->destroy(io); },
    "iterator", [] (clientio io) {
        clustername cluster((quickcheck()));
        agentname agent1((quickcheck()));
        peername::port port1((quickcheck()));
        agentname agent2((quickcheck()));
        while (agent1 == agent2) agent2 = quickcheck();
        peername::port port2((quickcheck()));
        agentname agent3((quickcheck()));
        while (agent1 == agent3 || agent2 == agent3) agent3 = quickcheck();
        peername::port port3((quickcheck()));
        auto c(beaconclient::build(cluster)
               .fatal("starting beacon client"));
        assert(c->start().finished());
        auto s1(beaconserver::build(
                    beaconserverconfig::dflt(cluster,
                                             agent1),
                    mklist(interfacetype::test),
                    port1)
                .fatal("starting beacon server"));
        auto s2(beaconserver::build(
                    beaconserverconfig::dflt(cluster,
                                             agent2),
                    mklist(interfacetype::storage,
                           interfacetype::test),
                    port2)
                .fatal("starting beacon server"));
        auto s3(beaconserver::build(
                    beaconserverconfig::dflt(cluster,
                                             agent3),
                    mklist(interfacetype::test2),
                    port3)
                .fatal("starting beacon server"));
        assert(c->query(io, agent1).name.getport() == port1);
        assert(c->query(io, agent2).name.getport() == port2);
        assert(c->query(io, agent3).name.getport() == port3);
        bool found1 = false;
        bool found2 = false;
        bool found3 = false;
        for (auto it(c->start()); !it.finished(); it.next()) {
            if (it.name() == agent1) {
                assert(!found1);
                assert(it.type() == mklist(interfacetype::test));
                assert(it.peer().getport() == port1);
                found1 = true; }
            else if (it.name() == agent2) {
                assert(!found2);
                assert(it.type().length() == 2);
                assert(it.type().idx(0) == interfacetype::storage);
                assert(it.type().idx(1) == interfacetype::test);
                assert(it.peer().getport() == port2);
                found2 = true; }
            else if (it.name() == agent3) {
                assert(!found3);
                assert(it.type() == mklist(interfacetype::test2));
                assert(it.peer().getport() == port3);
                found3 = true; }
            else abort(); }
        assert(found1);
        assert(found2);
        assert(found3);
        {   auto it(c->start(interfacetype::test2));
            assert(!it.finished());
            assert(it.type() == mklist(interfacetype::test2));
            assert(it.peer().getport() == port3);
            it.next();
            assert(it.finished()); }
        c->destroy();
        s1->destroy(io);
        s2->destroy(io);
        s3->destroy(io); },
    "clientconfig", [] { parsers::roundtrip(parsers::__beaconclientconfig()); },
    "serverconfig", [] { parsers::roundtrip(beaconserverconfig::parser()); },
    "badconfig", [] {
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
                   timedelta::seconds(-1)) == error::range); },
    "clientfailure1", [] (clientio) {
        tests::hook<orerror<udpsocket>, udpsocket> h(
            udpsocket::_client, [] (udpsocket u) {
                u.close();
                return error::pastend; });
        assert(beaconclient::build(
                   beaconclientconfig(clustername(quickcheck())))
               == error::pastend); },
    "clientfailure2", [] (clientio io) {
        unsigned errcount = 0;
        beaconclient *c;
        tests::hook<orerror<void>, const udpsocket &> h(
            udpsocket::_receive,
            [&c, &errcount] (const udpsocket &s) -> orerror<void> {
                if (c == NULL || s != c->__test_listenfd()) return Success;
                if (errcount++ < 3) return error::pastend;
                else return Success; });
        clustername cluster((quickcheck()));
        agentname agent((quickcheck()));
        peername::port port((quickcheck()));
        auto s(beaconserver::build(
                   beaconserverconfig::dflt(cluster, agent),
                   mklist(interfacetype::test),
                   port)
               .fatal("starting beacon server"));
        c = beaconclient::build(beaconclientconfig(cluster))
            .fatal("contructing beacon");
        auto tv(timedelta::time(
                    [c, port, &agent] {
                        assert(c->query(clientio::CLIENTIO, agent)
                               .name
                               .getport() == port); }));
        assert(errcount >= 3);
        assert(tv >= timedelta::milliseconds(200));
        assert(tv <= timedelta::milliseconds(400));
        s->destroy(io);
        c->destroy(); },
    "clientbroadcastfailure", [] (clientio io) {
        /* Client should be able to recover if its first few
         * broadcasts fail. */
        /* Start server first so that the server broadcast doesn't
         * save the client. */
        clustername cluster((quickcheck()));
        agentname agent((quickcheck()));
        peername::port port((quickcheck()));
        auto s(beaconserver::build(beaconserverconfig::dflt(cluster, agent),
                                   mklist(interfacetype::test),
                                   port)
               .fatal("starting beacon server"));
        unsigned cntr = 0;
        tests::hook<orerror<void>, const udpsocket &, const peername &> h(
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
        auto tv(timedelta::time([c, &agent, port, io] {
                    assert(c->query(io, agent).name.getport() == port); }));
        assert(cntr >= 3);
        /* First three broadcasts fail, and we do one every 100ms,
         * so it should take at least 200ms to complete
         * (fencepost). */
        assert(tv >= timedelta::milliseconds(200));
        /* Should complete fairly quickly after that. */
        assert(tv <= timedelta::milliseconds(400));
        s->destroy(io);
        c->destroy(); },
    "clientdirectfailure", [] (clientio io) {
        /* If we stop the client from doing directed calls then
         * things should drop out of the cache at the expiry time
         * and come back at the broadcast time. */
        clustername cluster((quickcheck()));
        agentname agent((quickcheck()));
        peername::port port((quickcheck()));
        auto s(beaconserver::build(
                   beaconserverconfig(beaconconfig::dflt,
                                      cluster,
                                      agent,
                                      timedelta::milliseconds(500)),
                   mklist(interfacetype::test),
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
        c->query(io, agent);
        bool blocksends = true;
        unsigned cntr = 0;
        tests::hook<orerror<void>, const udpsocket &, const peername &> h(
            udpsocket::_send,
            [&blocksends, c, &cntr]
            (const udpsocket &sock, const peername &p) -> orerror<void> {
                if (sock != c->__test_listenfd() &&
                    sock != c->__test_clientfd()) {
                    return Success; }
                else if (p.isbroadcast()) return Success;
                else if (!blocksends) return Success;
                cntr++;
                return error::pastend; });
        /* Poll to see when it drops out. */
        auto start(timestamp::now());
        while (c->poll(agent) != Nothing) {
            (timestamp::now() + timedelta::milliseconds(10)).sleep(io); }
        auto drop(timestamp::now());
        /* Should be near the expiry time. */
        assert(drop - start >= timedelta::milliseconds(400));
        assert(drop - start <= timedelta::milliseconds(600));
        /* Should have made about 5 attempts. */
        assert(cntr >= 3);
        assert(cntr <= 7);
        /* Poll to see when it comes back. */
        while (c->poll(agent) == Nothing) {
            (timestamp::now() + timedelta::milliseconds(10)).sleep(io); }
        auto recover(timestamp::now());
        assert(recover - start >= timedelta::milliseconds(900));
        assert(recover - start <= timedelta::milliseconds(1100));
        c->destroy();
        s->destroy(io); },
#if TESTING
    "sillyiterator", [] (clientio) {
        clustername cn((quickcheck()));
        beaconclientconfig config(cn, interfacetype::eq);
        auto c(beaconclient::build(config)
               .fatal("creating beacon client"));
        unsigned nr = 0;
        tests::eventwaiter< ::loglevel> waiter(
            tests::logmsg,
            [&nr] (loglevel level) { if (level >= loglevel::error) nr++; });
        auto it(c->start(interfacetype::storage));
        assert(nr > 0);
        assert(it.finished());
        c->destroy(); },
#endif
    "serverfailure1", [] (clientio) {
        tests::hook<orerror<udpsocket>, udpsocket> h(
            udpsocket::_client,
            [] (udpsocket) { return error::pastend; });
        assert(beaconserver::build(beaconserverconfig::dflt(quickcheck(),
                                                            quickcheck()),
                                   mklist(interfacetype::test),
                                   quickcheck())
               == error::pastend); },
#if TESTING
    "serverfailure2", [] (clientio io) {
        bool fail = false;
        tests::hook<orerror<void>, const udpsocket &> h(
            udpsocket::_receive,
            [&fail] (udpsocket) -> orerror<void> {
                if (fail) return error::pastend;
                else return Success; });
        unsigned nr = 0;
        tests::eventwaiter< ::loglevel> waiter(
            tests::logmsg,
            [&nr] (loglevel level) { if (level >= loglevel::info) nr++; });
        clustername cluster((quickcheck()));
        agentname agent((quickcheck()));
        peername::port port((quickcheck()));
        auto s(beaconserver::build(beaconserverconfig(
                                       beaconconfig::dflt,
                                       cluster,
                                       agent,
                                       timedelta::milliseconds(300)),
                                   mklist(interfacetype::test),
                                   port)
               .fatal("beaconserver::build"));
        auto c(beaconclient::build(
                   beaconclientconfig::mk(cluster,
                                          Nothing,
                                          Nothing,
                                          beaconconfig::dflt,
                                          timedelta::hours(5),
                                          timedelta::milliseconds(20))
                   .fatal("beaconclientconfig::mk"))
               .fatal("beaconclient::build"));
        assert(c->query(io, agent).name.getport() == port);
        fail = true;
        /* Wait long enough for it to drop out of the cache. */
        (timestamp::now() + timedelta::milliseconds(400)).sleep(io);
        assert(c->poll(agent) == Nothing);
        /* Failures should produce a plausible but not excessive
         * number of log messages. */
        assert(nr >= 3);
        assert(nr < 10);
        /* If the failure clears then we should be able to use the
         * server normally again. */
        fail = false;
        assert(c->query(io, agent).name.getport() == port);
        c->destroy();
        s->destroy(io); },
#endif
    "serverfailure3", [] (clientio io) {
        clustername cluster((quickcheck()));
        agentname agent((quickcheck()));
        peername::port port((quickcheck()));
        auto s(beaconserver::build(beaconserverconfig(
                                       beaconconfig::dflt,
                                       cluster,
                                       agent,
                                       timedelta::milliseconds(300)),
                                   mklist(interfacetype::test),
                                   port)
               .fatal("beaconserver::build"));
        /* Blocking the server from sending aything should make it
         * invisible to clients. */
        bool fail = true;
        tests::hook<orerror<void>, const udpsocket &, const peername &> h(
            udpsocket::_send,
            [&fail, s]
            (const udpsocket &sock, const peername &) -> orerror<void> {
                if (fail && sock == s->__test_clientfd()) return error::pastend;
                else return Success; });
        auto c(beaconclient::build(
                   beaconclientconfig::mk(
                       cluster,
                       Nothing,
                       Nothing,
                       beaconconfig::dflt,
                       timedelta::milliseconds(50),
                       timedelta::milliseconds(100))
                   .fatal("beaconclientconfig::mk"))
               .fatal("beaconclient::build"));
        assert(c->poll(agent) == Nothing);
        (timestamp::now() + timedelta::milliseconds(200)).sleep(io);
        assert(c->poll(agent) == Nothing);
        /* Stop injecting errors and make sure server starts working. */
        fail = false;
        assert(timedelta::time([c, io, port, &agent] {
                    assert(c->query(io, agent).name.getport() == port); })
            <= timedelta::milliseconds(300));
        c->destroy();
        s->destroy(io); }
#if TESTING
    ,
    "badserver", [] (clientio io) {
        quickcheck q;
        clustername cn(q);
        auto underlying(mkrandom<beaconconfig>(q));
        auto c(beaconclient::build(beaconclientconfig(cn,
                                                      Nothing,
                                                      Nothing,
                                                      underlying))
               .fatal("beaconclient::build"));
        /* Send a couple of deliberately malformed packets. */
        auto sender(udpsocket::client().fatal("udp client"));
        /* Shouldn't spam the logs too much */
        unsigned loudmsgs = 0;
        unsigned quietmsgs = 0;
        tests::eventwaiter< ::loglevel> waiter(
            tests::logmsg,
            [&loudmsgs, &quietmsgs] (loglevel level) {
                if (level > loglevel::debug) loudmsgs++;
                else quietmsgs++; });
        /* Something which can't be decoded at all */
        {   ::buffer b;
            serialise1(b).push((unsigned) 123);
            sender.send(b, peername::loopback(underlying.respport))
                .fatal("sending bad packet"); }
        /* Something which decodes and gives a bad version. */
        peername::port port(q);
        agentname sn(q);
        {   ::buffer b;
            interfacetype t(q);
            timedelta ct(q);
            proto::beacon::resp rsp(cn,
                                    sn,
                                    list<interfacetype>::mk(t),
                                    port,
                                    ct);
            rsp.version = ::version::invalid;
            serialise1(b).push(rsp);
            sender.send(b,
                        peername::loopback(underlying.respport))
                .fatal("sending bad version packet"); }
        sender.close();
        /* Give it a moment to go through. */
        timedelta::milliseconds(100).future().sleep(io);
        assert(quietmsgs > 2);
        assert(loudmsgs == 0);
        assert(c->poll(sn) == Nothing);
        /* Make sure that the client still works. */
        auto s(beaconserver::build(
                   beaconserverconfig(
                       underlying,
                       cn,
                       sn,
                       timedelta::seconds(1)),
                   mklist(interfacetype::test),
                   port)
               .fatal("starting beacon server"));
        assert(c->query(io, sn).name.getport() == port);
        c->status(loglevel::emergency);
        assert(loudmsgs > 0);
        s->destroy(io);
        c->destroy(); },
    "badclient", [] (clientio io) {
        quickcheck q;
        clustername cn(q);
        auto underlying(mkrandom<beaconconfig>(q));
        peername::port port(q);
        agentname sn(q);
        auto s(beaconserver::build(
                   beaconserverconfig(
                       underlying,
                       cn,
                       sn,
                       timedelta::seconds(1)),
                   mklist(interfacetype::test),
                   port)
               .fatal("starting beacon server"));
        unsigned msgs = 0;
        tests::eventwaiter< ::loglevel> waiter(
            tests::logmsg,
            [&msgs] (loglevel level) { if (level>loglevel::debug) msgs++;});
        /* Send a bad request */
        {   auto sender(udpsocket::client().fatal("udp client"));
            ::buffer b;
            serialise1(b).push((unsigned) 123);
            sender.send(b, peername::loopback(underlying.reqport))
                .fatal("sending bad packet");
            sender.close(); }
        /* Shouldn't produce any log messages. */
        timedelta::milliseconds(100).future().sleep(io);
        assert(msgs == 0);
        /* Check the server still works. */
        auto c(beaconclient::build(beaconclientconfig(cn,
                                                      Nothing,
                                                      Nothing,
                                                      underlying))
               .fatal("beaconclient::build"));
        assert(c->query(io, sn).name.getport() == port);
        s->destroy(io);
        c->destroy(); }
#endif
    );
