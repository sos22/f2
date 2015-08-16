/* This is a bit of a grab bag of things vaguely related to low-level
 * job infrastructure. */
#include "eqclient.H"
#include "clientio.H"
#include "computeagent.H"
#include "computeclient.H"
#include "test2.H"

#include "either.tmpl"
#include "test2.tmpl"

static testmodule __testcomputeagent(
    "computeagent",
    list<filename>::mk("compute.C",
                       "compute.H",
                       "computeclient.C",
                       "computeclient.H",
                       "computeagent.C",
                       "computeagent.H",
                       "jobapi.C",
                       "jobapi.H",
                       "jobapiimpl.H",
                       "jobname.C",
                       "jobname.H",
                       "jobresult.C",
                       "jobresult.H",
                       "testjob.C"),
    testmodule::LineCoverage(75_pc),
    testmodule::BranchCoverage(45_pc),
    testmodule::Dependency("testjob.so"),
    "basics", [] (clientio io) {
        quickcheck q;
        clustername cluster(q);
        /* Always need an fs agent name to start the compute agent,
         * but if no job touches the filesystem don't actually need an
         * fs agent to go with it. */
        agentname fsagentname("fsagent");
        agentname computeagentname("computeagent");
        filename computedir(q);
        computeagent::format(computedir)
            .fatal("formatting compute state dir");
        auto &cp(*connpool::build(cluster)
                 .fatal("building connpool"));
        auto &computeagent(*computeagent::build(io,
                                                cluster,
                                                fsagentname,
                                                computeagentname,
                                                computedir)
                           .fatal("starting compute agent"));
        auto &cc(computeclient::connect(cp, computeagentname));
        assert(cc
               .enumerate(io)
               .fatal("getting initial empty job list")
               .second()
               .empty());
        auto &eqc(
            *eqclient<proto::compute::event>::connect(
                io,
                cp,
                computeagentname,
                proto::eq::names::compute,
                (30_s).future())
            .fatal("connecting to compute event queue")
            .first());
        assert(eqc.pop() == Nothing);
        deserialise1 ds(q);
        job j(
            filename("./testjob.so"),
            "testfunction",
            empty,
            empty);
        auto startres(cc.start(io, j).fatal("starting job"));
        logmsg(loglevel::debug, "startres " + startres.field());
        auto startev(eqc.popid(io).fatal("getting first event"));
        logmsg(loglevel::debug, "startev " + startev.field());
        assert(startev.second().start().just().first() == j.name());
        assert(startev.first() == startres.first());
        {   auto l(cc
                   .enumerate(io)
                   .fatal("getting extended job list")
                   .second());
            assert(l.length() == 1);
            assert(l.idx(0).name == j.name()); }
        {   auto finishev(
                eqc
                .pop(io)
                .fatal("getting second event")
                .finish()
                .just());
            assert(finishev.first() == j.name());
            assert(finishev
                   .second()
                   .first()
                   .success()
                   .issuccess()); }
        cc.drop(io, j.name()).fatal("dropping job");
        assert(cc
               .enumerate(io)
               .fatal("getting final empty job list")
               .second()
               .empty());
        cc.destroy();
        computeagent.destroy(io);
        computedir.rmtree().fatal("removing compute tree"); },
    "shutdown", [] (clientio io) {
        quickcheck q;
        clustername cluster(q);
        agentname fsagentname("fsagent");
        agentname computeagentname("computeagent");
        filename computedir(q);
        computeagent::format(computedir)
            .fatal("formatting compute state dir");
        auto &cp(*connpool::build(cluster)
                 .fatal("building connpool"));
        auto &computeagent(*computeagent::build(io,
                                                cluster,
                                                fsagentname,
                                                computeagentname,
                                                computedir)
                           .fatal("starting compute agent"));
        auto &cc(computeclient::connect(cp, computeagentname));
        job j(
            filename("./testjob.so"),
            "waitforever",
            empty,
            empty);
        auto startres(cc.start(io, j).fatal("starting job"));
        (1_s).future().sleep(io);
        /* Job should still be running */
        {   auto l(cc
                   .enumerate(io)
                   .fatal("getting extended job list")
                   .second());
            assert(l.length() == 1);
            assert(l.idx(0).name == j.name());
            assert(l.idx(0).result == Nothing); }
        /* Should be able to shut the agent down reasonably
         * promptly. */
        assert(timedelta::time([&] { computeagent.destroy(io); }) < 1_s);
        cc.destroy();
        cp.destroy();
        computedir.rmtree().fatal("remove compute dir "+computedir.field()); });
