/* Test for the storage agent and storage client bits. */
#include "connpool.H"
#include "filename.H"
#include "list.H"
#include "job.H"
#include "jobname.H"
#include "storageagent.H"
#include "storageclient.H"
#include "test2.H"

#include "list.tmpl"
#include "test2.tmpl"

class teststate {
public: quickcheck q;
public: filename pool;
public: orerror<void> fmtres;
public: clustername cn;
public: agentname an;
public: storageconfig config;
public: storageagent &agent;
public: connpool &cp;
public: storageclient &client;
public: teststate(clientio io)
    : q(),
      pool(filename::mktemp(q).fatal("mktemp")),
      fmtres(storageagent::format(pool)),
      cn(q),
      an(q),
      config(pool, beaconserverconfig::dflt(cn, an)),
      agent(storageagent::build(io, config)
            .fatal("starting storage agent")),
      cp(connpool::build(cn).fatal("building conn pool")),
      client(storageclient::connect(io, cp, an)
             .fatal("connecting to agent")) {
    fmtres.fatal("formatting storage area"); }
public: ~teststate() {
    agent.destroy(clientio::CLIENTIO);
    client.destroy();
    cp.destroy();
    pool.rmtree().fatal("removing " + pool.field()); } };

static testmodule __teststorageagent(
    "storageagent",
    list<filename>::mk("storageagent.C",
                       "storageagent.H",
                       "storageclient.C",
                       "storageclient.H"),
    "connect", [] (clientio io) { teststate t((io)); },
    "emptyjob", [] (clientio io) {
        teststate t((io));
        assert(t.client
               .listjobs(io)
               .fatal("listing jobs")
               .second()
               .length()
               == 0);
        deserialise1 ds(t.q);
        job j(ds);
        t.client.createjob(io, j).fatal("creating job");
        {   auto r(t.client.listjobs(io).fatal("listing jobs").second());
            assert(r.length() == 1);
            assert(r.idx(0) == j.name());
            assert(t.client.statjob(io, j.name()) == j); }
        {   auto r(t.client
                   .liststreams(io, j.name())
                   .fatal("listing streams")
                   .second());
            assert(r.length() == j.outputs().length());
            for (auto it(r.start()); !it.finished(); it.next()) {
                /* Cannot be any dupes in the list. */
                {   auto it2(it);
                    for (it2.next(); !it2.finished(); it2.next()) {
                        assert(it->name() != it2->name()); } }
                /* All returned streams must be in the job */
                assert(j.outputs().contains(it->name()));
                /* Returned streams must be empty */
                assert(!it->isfinished());
                assert(it->size() == 0_B); }
            /* All of the job's streams must be in the list. */
            for (auto it(j.outputs().start()); !it.finished(); it.next()) {
                bool found = false;
                for (auto it2(r.start()); !found && !it2.finished();it2.next()){
                    found = *it == it2->name(); } } }
        t.client.removejob(io, j.name())
            .fatal("removing job");
        assert(t.client.statjob(io, j.name()) == error::notfound);
        assert(t.client
               .listjobs(io)
               .fatal("listing jobs")
               .second()
               .length()
               == 0);
    } );
