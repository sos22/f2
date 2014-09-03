/* Very basic system-level smoke tests for the master: can it start,
 * does it expose a control interface, can a storage slave connect,
 * does it notice when a storage slave gets SIGSTOPped, does it notice
 * when a storage slave gets SIGKILLed. */
#include <sys/types.h>
#include <sys/wait.h>
#include <err.h>
#include <signal.h>
#include <unistd.h>

#include "beaconclient.H"
#include "buildconfig.H"
#include "coordinator.H"
#include "fields.H"
#include "filename.H"
#include "logging.H"
#include "masterconfig.H"
#include "peername.H"
#include "proto.H"
#include "pubsub.H"
#include "quickcheck.H"
#include "registrationsecret.H"
#include "rpcconn.H"
#include "shutdown.H"
#include "spawn.H"
#include "storageconfig.H"
#include "timedelta.H"
#include "timestamp.H"

#include "either.tmpl"
#include "list.tmpl"
#include "rpcconn.tmpl"

int
main(int argc, char *argv[]) {
    if (argc != 2) errx(1, "need one argument, the test directory");

   initlogging("testmaster");
    initpubsub();
    filename testdir(argv[1]);
    testdir.mkdir().fatal("creating " + fields::mk(testdir));
    quickcheck q;
    peername::port beaconport(q);
    auto mastercontrolsock(peername::local(testdir + "mastercontrol")
                           .fatal("mastercontrol socket"));
    registrationsecret rs(q);
    /* Start a new master */
    /* Make ping machine a bit more aggressive so that the test runs a
     * bit more quickly. */
    rpcconnconfig masterconnconfig(rpcconnconfig::dflt);
    masterconnconfig.pingdeadline = timedelta::seconds(10);
    masterconfig msc(masterconfig(rs)
                     ._controlsock(mastercontrolsock)
                     ._beaconport(beaconport)
                     ._listenon(peername::loopback(peername::port::any))
                     ._connconfig(masterconnconfig));
    auto master(
        spawn::process::spawn(
            spawn::program(buildconfig::us.programname("master"))
            .addarg(fields::mk(msc)))
        .fatal("starting master"));

    /* Connect to its control interface.  Should take less than a
       second to get going. */
    rpcconn *masterconn;
    {   auto start(timestamp::now());
        auto deadline(start + timedelta::seconds(1));
        while (1) {
            auto c(rpcconn::connect<rpcconn>(
                       clientio::CLIENTIO,
                       rpcconnauth::mkdone(
                           slavename("<master server>"),
                           actortype::test,
                           rpcconnconfig::dflt),
                       mastercontrolsock,
                       rpcconnconfig::dflt));
            if (c.issuccess()) {
                masterconn = c.success();
                break; }
            if (timestamp::now() > deadline) {
                c.fatal("Connecting to master; started at " +
                        fields::mk(start) + ", now " +
                        fields::mk(timestamp::now())); }
            (timestamp::now() + timedelta::milliseconds(50)).sleep(); }
        logmsg(loglevel::info,
               "master took " + fields::mk(timestamp::now() - start) +
               " to start accepting control connections"); }

    /* Give it a PING to confirm that it's alive.  Should respond
     * within 100ms. */
    delete masterconn->call(
        clientio::CLIENTIO,
        wireproto::req_message(proto::PING::tag,
                               masterconn->allocsequencenr()),
        timestamp::now() + timedelta::milliseconds(100))
        .fatal("initial PING");

    /* Check that it initially has no slaves. */
    {   auto m(masterconn->call(
                   clientio::CLIENTIO,
                   wireproto::req_message(proto::STATUS::tag,
                                          masterconn->allocsequencenr()),
                   timestamp::now() + timedelta::milliseconds(100))
               .fatal("first STATUS"));
        const auto &s(m->getparam(proto::STATUS::resp::coordinator)
                      .fatal(fields::mk("missing coordinator status?")));
        assert(s.conns.empty());
        delete m; }

    /* Start a storage slave. */
    rpcconnconfig slaveconnconfig(rpcconnconfig::dflt);
    slaveconnconfig.pingdeadline = timedelta::seconds(10);
    auto slave(
        spawn::process::spawn(
            spawn::program(buildconfig::us.programname("storage"))
            .addarg(
                fields::mk(
                    storageconfig(peername::local(testdir + "storagecontrol")
                                  .fatal("storagecontrolsock"),
                                  testdir + "pool",
                                  beaconclientconfig(rs)
                                  .connectto(peername::loopback(
                                                 msc.beaconport)),
                                  slavename("storageslave"),
                                  peername::loopback(peername::port::any),
                                  slaveconnconfig))))
        .fatal("starting storage slave"));

    /* New slave should appear in connection list fairly quickly. */
    {   auto start(timestamp::now());
        auto deadline(start + timedelta::seconds(1));
        while (1) {
            auto m(masterconn->call(
                       clientio::CLIENTIO,
                       wireproto::req_message(proto::STATUS::tag,
                                              masterconn->allocsequencenr()),
                       timestamp::now() + timedelta::milliseconds(100))
                   .fatal("secondary STATUS"));
            auto s(m->getparam(proto::STATUS::resp::coordinator)
                   .fatal(fields::mk("missing coordinator status?")));
            if (!s.conns.empty()) {
                assert(s.conns.length() == 1);
                if (s.conns.idx(0).name == Nothing) {
                    /* Raced with slave connection.  Keep waiting. */
                    delete m; }
                else {
                    /* Slave connected */
                    assert(s.conns.idx(0).name == slavename("storageslave"));
                    delete m;
                    break; } }
            else {
                delete m; }
            auto now(timestamp::now());
            if (now > deadline) {
                error::timeout.fatal(
                    "waiting for storage slave to connect to master"); }
            (now + timedelta::milliseconds(20)).sleep(); }
        logmsg(loglevel::info,
               "storage slave connected in " +
               fields::mk(timestamp::now() - start)); }

    /* Stop the slave -> it should drop out of the list, but that
     * might take a bit longer. */
    auto slavepausetok(slave->pause());
    {   auto start(timestamp::now());
        auto deadline(start +
                      masterconnconfig.pingdeadline +
                      masterconnconfig.pinginterval +
                      timedelta::milliseconds(500));
        while (1) {
            auto m(masterconn->call(
                       clientio::CLIENTIO,
                       wireproto::req_message(proto::STATUS::tag,
                                              masterconn->allocsequencenr()),
                       timestamp::now() + timedelta::milliseconds(100))
                   .fatal("secondary STATUS"));
            auto s(m->getparam(proto::STATUS::resp::coordinator)
                   .fatal(fields::mk("missing coordinator status?")));
            if (s.conns.empty()) {
                delete m;
                break; }
            delete m;
            auto now(timestamp::now());
            if (now > deadline) {
                error::timeout.fatal(
                    "waiting for master to notice slave had stopped"); }
            (now + timedelta::milliseconds(100)).sleep(); }
        /* Shouldn't drop out for interruptions less than the ping
         * deadline. */
        assert(timestamp::now() > start + masterconnconfig.pingdeadline);
        logmsg(loglevel::info,
               "master noticed slave stopped in " +
               fields::mk(timestamp::now() - start)); }

    /* Restarting the slave should cause it re-register with master,
     * eventually. */
    /* (This is more of a test of the slave than the master) */
    slave->unpause(slavepausetok);
    {   auto start(timestamp::now());
        /* Slave might have to run its ping machine to notice it's
         * been disconnected. */
        auto deadline(start +
                      slaveconnconfig.pinginterval +
                      slaveconnconfig.pingdeadline +
                      timedelta::milliseconds(500));
        while (1) {
            auto m(masterconn->call(
                       clientio::CLIENTIO,
                       wireproto::req_message(proto::STATUS::tag,
                                              masterconn->allocsequencenr()),
                       timestamp::now() + timedelta::milliseconds(100))
                   .fatal("secondary STATUS"));
            auto s(m->getparam(proto::STATUS::resp::coordinator)
                   .fatal(fields::mk("missing coordinator status")));
            logmsg(loglevel::info, "slaves: " + fields::mk(s.conns));
            if (!s.conns.empty()) {
                assert(s.conns.length() == 1);
                if (s.conns.idx(0).name == Nothing) {
                    delete m; }
                else {
                    assert(s.conns.idx(0).name == slavename("storageslave"));
                    delete m;
                    break; } }
            else {
                delete m; }
            auto now(timestamp::now());
            if (now > deadline) {
                error::timeout.fatal(
                    "waiting for master to notice slave had restarted"); }
            (now + timedelta::milliseconds(100)).sleep(); }
        logmsg(loglevel::info,
               "master noticed slave restarted in " +
               fields::mk(timestamp::now() - start)); }

    /* Killing a slave should cause it drop out of the master list
     * fairly quickly.  The master should get error::disconnected on
     * its next ping, so we'll only have to wait for the ping interval
     * and not the ping deadline. */
    slave->kill();
    {   auto start(timestamp::now());
        auto deadline(start +
                      masterconnconfig.pinginterval +
                      timedelta::milliseconds(500));
        while (1) {
            auto m(masterconn->call(
                       clientio::CLIENTIO,
                       wireproto::req_message(proto::STATUS::tag,
                                              masterconn->allocsequencenr()),
                       timestamp::now() + timedelta::milliseconds(100))
                   .fatal("secondary STATUS"));
            const auto &s(m->getparam(proto::STATUS::resp::coordinator)
                          .fatal(fields::mk("missing coordinator status")));
            if (s.conns.empty()) {
                delete m;
                break; }
            delete m;
            auto now(timestamp::now());
            if (now > deadline) {
                error::timeout.fatal(
                    "waiting for master to notice slave had died"); }
            (now + timedelta::milliseconds(100)).sleep(); }
        logmsg(loglevel::info,
               "master noticed slave stopped in " +
               fields::mk(timestamp::now() - start)); }

    /* Shut the master down cleanly. */
    masterconn->send(clientio::CLIENTIO,
                     wireproto::tx_message(proto::QUIT::tag)
                     .addparam(proto::QUIT::req::message, "die")
                     .addparam(proto::QUIT::req::reason, shutdowncode(7)))
        .fatal("sending QUIT message");
    masterconn->drain(clientio::CLIENTIO);
    auto r(master->join(clientio::CLIENTIO));
    assert(r.left() == shutdowncode(7));
    masterconn->destroy(clientio::CLIENTIO);
    logmsg(loglevel::info, fields::mk("Test passed"));
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging();
    return 0; }
