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

static testmodule __teststorageagent(
    "storageagent",
    list<filename>::mk("storageagent.C",
                       "storageagent.H",
                       "storageclient.C",
                       "storageclient.H"),
    "basics", [] (clientio io) {
        quickcheck q;
        auto pool(filename::mktemp(q).fatal("mktemp"));
        clustername cn(q);
        agentname an(q);
        storageagent::format(pool).fatal("mkpool");
        storageconfig config(pool, beaconserverconfig::dflt(cn, an));
        auto agent(storageagent::build(io, config)
                   .fatal("starting storage agent"));
        auto cp(connpool::build(cn).fatal("building conn pool"));
        auto client(storageclient::connect(io, cp, an)
                    .fatal("connecting to agent"));
        /* Start off with no jobs. */
        assert(client->listjobs(io).fatal("list jobs").empty());
        /* Should be able to create something. */
        job j("init.so",
              "init",
              map<streamname, job::inputsrc>(),
              list<streamname>());
        client->createjob(io, j).fatal("creating job");
        /* And we should then be able to read it back. */
        assert(client
               ->listjobs(io)
               .fatal("second list jobs") ==
               list<job>::mk(j));
        /* Shut down and restart server and confirm that it was
         * persistent. */
        agent->destroy(io);
        agent = storageagent::build(io, config)
            .fatal("restarting storage agent");
        assert(client
               ->listjobs(io)
               .fatal("second list jobs") ==
               list<job>::mk(j));
        /* Done. */
        agent->destroy(io);
        client->destroy();
        cp->destroy();
        pool.rmtree().fatal("removing " + pool.field()); });
