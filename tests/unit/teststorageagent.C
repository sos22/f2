/* Test for the storage agent and storage client bits. */
#include "connpool.H"
#include "filename.H"
#include "list.H"
#include "job.H"
#include "jobname.H"
#include "storageagent.H"
#include "storageclient.H"
#include "test2.H"

#include "either.tmpl"
#include "list.tmpl"
#include "pair.tmpl"
#include "test2.tmpl"

class teststate {
public: quickcheck q;
public: filename pool;
public: orerror<void> fmtres;
public: clustername cn;
public: agentname an;
public: storageagent &agent;
public: connpool &cp;
public: storageclient &client;
public: teststate(clientio io)
    : q(),
      pool(filename::mktemp(q).fatal("mktemp")),
      fmtres(storageagent::format(pool)),
      cn(q),
      an(q),
      agent(storageagent::build(io, cn, an, pool)
            .fatal("starting storage agent")),
      cp(connpool::build(cn).fatal("building conn pool")),
      client(storageclient::connect(cp, an)) {
    fmtres.fatal("formatting storage area"); }
public: ~teststate() {
    agent.destroy(clientio::CLIENTIO);
    client.destroy();
    cp.destroy();
    pool.rmtree().fatal("removing " + pool.field()); } };

static testmodule __teststorageagent(
    "storageagent",
    list<filename>::mk("storage.C",
                       "storage.H",
                       "storageagent.C",
                       "storageagent.H",
                       "storageclient.C",
                       "storageclient.H"),
    testmodule::LineCoverage(79_pc),
    testmodule::BranchCoverage(45_pc),
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
                assert(it->size() == 0_B);
                /* stating it must return the same thing. */
                assert(t.client.statstream(io, j.name(), it->name()) == *it); }
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
               == 0); },
    "basicjob", [] (clientio io) {
        teststate t((io));
        auto sn(streamname::mk("X").fatal("X"));
        job j(filename("dummy.so"),
              string("dummyfn"),
              map<streamname, job::inputsrc>(),
              list<streamname>::mk(sn));
        auto jn(j.name());
        t.client.createjob(io, j).fatal("creating job");
        assert(t.client.read(io, jn, sn) == error::toosoon);
        t.client
            .append(io, jn, sn, buffer("foo"), 0_B)
            .fatal("appending 1");
        {   buffer b("bar");
            t.client
                .append(io, jn, sn, Steal, b, 3_B)
                .fatal("appending 1");
            assert(b.empty()); }
        assert(t.client.read(io, jn, sn) == error::toosoon);
        t.client.finish(io, jn, sn).fatal("finishing stream");
        auto r(t.client.read(io, jn, sn).fatal("read"));
        assert(r.first() == 6_B);
        assert(r.second().contenteq(buffer("foobar"))); },
    "asyncstatjob", [] (clientio io) {
        teststate t((io));
        auto sn(streamname::mk("X").fatal("X"));
        job j(filename("dummy.so"),
              string("dummyfn"),
              map<streamname, job::inputsrc>(),
              list<streamname>::mk(sn));
        auto jn(j.name());
        t.client.createjob(io, j).fatal("creating job");
        auto pt(t.agent.pause(io));
        auto start(timestamp::now());
        auto &statjob(t.client.statjob(jn));
        auto end(timestamp::now());
        assert(end - start < 10_ms);
        assert(statjob.finished() == Nothing);
        start = end;
        end = timestamp::now();
        assert(end - start < 10_ms);
        (50_ms).future().sleep(io);
        assert(statjob.finished() == Nothing);
        t.agent.unpause(pt);
        start = timestamp::now();
        auto res(statjob.pop(io));
        end = timestamp::now();
        assert(end - start < 10_ms);
        assert(res.issuccess());
        assert(res.success() == j); } );
