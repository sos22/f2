/* The storage service is a fairly simple wrapper around the storage
 * agent, and this only covers the service, so it's mostly just a few
 * basic sanity checks on the argument parser. */
#include "connpool.H"
#include "spawn.H"
#include "storage.H"
#include "storageconfig.H"
#include "test2.H"

#include "connpool.tmpl"
#include "either.tmpl"
#include "fields.tmpl"
#include "maybe.tmpl"
#include "test2.tmpl"

const filename storageservice(PREFIX "/storageservice" EXESUFFIX);
const filename storagefmt(PREFIX "/storagefmt" EXESUFFIX);

static testmodule __teststorageservice(
    "storageservice",
    list<filename>::mk("storagefmt.C", "storageservice.C"),
    testmodule::Dependency("spawnservice" EXESUFFIX),
    testmodule::Dependency("storagefmt" EXESUFFIX),
    testmodule::Dependency("storageservice" EXESUFFIX),
    testmodule::BranchCoverage(60_pc),
    "missingarg", [] (clientio io) {
        assert(spawn::process::spawn(spawn::program(storageservice))
               .fatal("starting storageservice")
               ->join(io)
               .left()
               == shutdowncode(1)); },
    "missingargfmt", [] (clientio io) {
        assert(spawn::process::spawn(spawn::program(storagefmt))
               .fatal("starting storagefmt")
               ->join(io)
               .left()
               == shutdowncode(1)); },
    "unparseable", [] (clientio io) {
        assert(spawn::process::spawn(
                   spawn::program(storageservice)
                   .addarg("ceci n'est pas le config"))
               .fatal("starting storageservice")
               ->join(io)
               .left()
               == shutdowncode(1)); },
    "badpooldir", [] (clientio io) {
        assert(spawn::process::spawn(
                   spawn::program(storageservice)
                   .addarg(
                       fields::mk(
                           storageconfig(
                               filename("nodir"),
                               beaconserverconfig::dflt(
                                   clustername::mk("dummycluster").fatal("mkc"),
                                   agentname("dummyagent"))))))
               .fatal("starting storageservice")
               ->join(io)
               .left()
               == shutdowncode(1)); },
    "unformatted", [] (clientio io) {
        quickcheck q;
        auto f(filename::mktemp(q).fatal("mktemp"));
        f.mkdir().fatal("making " + f.field());
        assert(spawn::process::spawn(
                   spawn::program(storageservice)
                   .addarg(
                       fields::mk(
                           storageconfig(
                               f,
                               beaconserverconfig::dflt(
                                   clustername::mk("dummycluster").fatal("mkc"),
                                   agentname("dummyagent"))))))
               .fatal("starting storageservice")
               ->join(io)
               .left()
               == shutdowncode(1));
        f.rmdir().fatal("removing " + f.field()); },
    "realstart", [] (clientio io) {
        quickcheck q;
        clustername cn(q);
        agentname an(q);
        auto f(filename::mktemp(q).fatal("mktemp"));
        assert(spawn::process::spawn(spawn::program(storagefmt).addarg(f.str()))
               .fatal("starting spawnfmt")
               ->join(io)
               .left()
               == shutdowncode(0));
        assert(f.isdir().fatal("h"));
        auto ss(
            spawn::process::spawn(
                spawn::program(storageservice)
                .addarg(
                    fields::mk(
                        storageconfig(f, beaconserverconfig::dflt(cn, an)))))
               .fatal("starting storageservice"));
        /* Quick check that there is actually a storage agent running.
         * Testing the storage agent itself isn't the responsibility
         * of this test module. */
        auto pool(connpool::build(cn).fatal("building conn pool"));
        auto r(pool->call<proto::storage::listjobsres>(
                   io,
                   an,
                   interfacetype::storage,
                   (5_s).future(),
                   [] (serialise1 &s, connpool::connlock) {
                       proto::storage::tag::listjobs.serialise(s);
                       maybe<jobname>(Nothing).serialise(s);
                       maybe<unsigned>(Nothing).serialise(s); },
                   [] (deserialise1 &ds, connpool::connlock) {
                       proto::storage::listjobsres res(ds);
                       return res; })
               .fatal("calling new storage service"));
        /* Storage agents start empty. */
        assert(r.res.empty());
        /* Kill it again. */
        ss->kill();
        f.rmtree().fatal("removing " + f.field()); });
