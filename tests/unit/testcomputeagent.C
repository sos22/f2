/* This is a bit of a grab bag of things vaguely related to low-level
 * job infrastructure. */
#include "eqclient.H"
#include "clientio.H"
#include "computeagent.H"
#include "computeclient.H"
#include "test2.H"

#include "either.tmpl"
#include "test2.tmpl"

class computetest {
public:  quickcheck q;
public:  const clustername cluster;
public:  const agentname fsagentname;
public:  const agentname computeagentname;
public:  const filename computedir;
private: const orerror<void> computeformatres;
public:  connpool &cp;
public:  ::computeagent *computeagent;
public:  computeclient &cc;
public:  explicit computetest(clientio io)
    : q(),
      cluster(q),
      fsagentname("fsagent"),
      computeagentname("computeagent"),
      computedir(q),
      computeformatres(::computeagent::format(computedir)),
      cp(*connpool::build(cluster).fatal("building connpool")),
      computeagent(computeagent::build(io,
                                       cluster,
                                       fsagentname,
                                       computeagentname,
                                       computedir)
                   .fatal("starting compute agent")),
      cc(computeclient::connect(cp, computeagentname)) {
    computeformatres.fatal("formatting compute agent dir"); }
public:  ~computetest() {
    cc.destroy();
    if (computeagent != NULL) computeagent->destroy(clientio::CLIENTIO);
    cp.destroy();
    computedir.rmtree().fatal("removing compute dir"); } };

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
        computetest t(io);
        auto &cc(t.cc);
        auto &cp(t.cp);
        auto &computeagentname(t.computeagentname);
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
               .empty()); },
    "shutdown", [] (clientio io) {
        computetest t(io);
        job j(
            filename("./testjob.so"),
            "waitforever",
            empty,
            empty);
        auto startres(t.cc.start(io, j).fatal("starting job"));
        (1_s).future().sleep(io);
        /* Job should still be running */
        {   auto l(t
                   .cc
                   .enumerate(io)
                   .fatal("getting extended job list")
                   .second());
            assert(l.length() == 1);
            assert(l.idx(0).name == j.name());
            assert(l.idx(0).result == Nothing); }
        /* Should be able to shut the agent down reasonably
         * promptly. */
        assert(timedelta::time([&] { t.computeagent->destroy(io); }) < 1_s);
        t.computeagent = NULL; });
