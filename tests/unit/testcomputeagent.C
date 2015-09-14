/* This is a bit of a grab bag of things vaguely related to low-level
 * job infrastructure. */
#include "eqclient.H"
#include "clientio.H"
#include "computeagent.H"
#include "computeclient.H"
#include "filesystemagent.H"
#include "filesystemclient.H"
#include "rpcservice2.H"
#include "storageagent.H"
#include "storageclient.H"
#include "test2.H"

#include "either.tmpl"
#include "parsers.tmpl"
#include "test2.tmpl"

class computetest {
public:  quickcheck q;
public:  const clustername cluster;
public:  const agentname fsagentname;
public:  const agentname computeagentname;
public:  const agentname storageagentname;
public:  const filename computedir;
private: const orerror<void> computeformatres;
public:  const filename storagedir;
private: const orerror<void> storageformatres;
public:  connpool &cp;
public:  ::storageagent &storageagent;
public:  rpcservice2 &filesystemagent;
public:  ::computeagent *computeagent;
public:  storageclient &sc;
public:  filesystemclient &fsc;
public:  computeclient &cc;
public:  explicit computetest(clientio io)
    : q(),
      cluster(mkrandom<clustername>(q)),
      fsagentname("fsagent"),
      computeagentname("computeagent"),
      storageagentname("storageagent"),
      computedir(q),
      computeformatres(::computeagent::format(computedir)),
      storagedir(q),
      storageformatres(::storageagent::format(storagedir)),
      cp(*connpool::build(cluster).fatal("building connpool")),
      storageagent(*::storageagent::build(
                       io,
                       cluster,
                       storageagentname,
                       storagedir)
                   .fatal("starting storage agent")),
      filesystemagent(*::filesystemagent(
                          io,
                          cluster,
                          fsagentname,
                          peername::all(peername::port::any))
                      .fatal("starting filesystem agent")),
      computeagent(computeagent::build(io,
                                       cluster,
                                       fsagentname,
                                       computeagentname,
                                       computedir)
                   .fatal("starting compute agent")),
      sc(storageclient::connect(cp, storageagentname)),
      fsc(filesystemclient::connect(cp, fsagentname)),
      cc(computeclient::connect(cp, computeagentname)) {
    computeformatres.fatal("formatting compute agent dir"); }
    /* Create a job on the storage agent and wait for the filesystem
     * to pick it up. */
public:  void createjob(clientio io, const job &j) {
    auto createevt(sc.createjob(io, j).fatal("creating job"));
    fsc.storagebarrier(io, storageagentname, createevt)
        .fatal("awaiting job creation"); }
public:  ~computetest() {
    cc.destroy();
    fsc.destroy();
    sc.destroy();
    if (computeagent != NULL) computeagent->destroy(clientio::CLIENTIO);
    filesystemagent.destroy(clientio::CLIENTIO);
    storageagent.destroy(clientio::CLIENTIO);
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
    testmodule::BranchCoverage(50_pc),
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
        t.createjob(io, j);
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
                   .fatal("job result")
                   .issuccess()); }
        cc.drop(io, j.name()).fatal("dropping job");
        assert(cc
               .enumerate(io)
               .fatal("getting final empty job list")
               .second()
               .empty());
        eqc.destroy(); },
    "waitforjob", [] (clientio io) {
        computetest t(io);
        job j(
            filename("./testjob.so"),
            "waitonesecond",
            empty,
            empty);
        t.createjob(io, j);
        assert(t.cc.waitjob(io, j.name()) == error::toosoon);
        auto startres(t.cc.start(io, j).fatal("starting job"));
        auto taken(timedelta::time([&] {
                    assert(t.cc.waitjob(io, j.name())
                           .fatal("waitjob")
                           .fatal("waitjob inner")
                           .issuccess()); }));
        assert(taken > 900_ms);
        assert(taken < 1100_ms);
        t.cc.drop(io, j.name()).fatal("dropjob");
        assert(t.cc.waitjob(io, j.name()) == error::toosoon); },
    "finishstreams", [] (clientio io) {
        computetest t(io);
        auto ss(streamname::mk("outstream").fatal("mkstreamname"));
        job j(
            filename("./testjob.so"),
            "testfunction",
            empty,
            list<streamname>::mk(ss));
        agentname storageagentname("storageagent");
        filename storagedir(t.q);
        storageagent::format(storagedir).fatal("format storage agent");
        t.createjob(io, j);
        assert(t.sc.statstream(io, j.name(), ss)
               .fatal("statstream")
               .isempty());
        auto startres(t.cc.start(io, j).fatal("starting job"));
        assert(t.cc.waitjob(io, j.name())
               .fatal("waitjob")
               .fatal("waitjob inner")
               .issuccess());
        assert(t.sc.statstream(io, j.name(), ss)
               .fatal("statstream2")
               .isfinished()); },
    "jobresultparse", [] { parsers::roundtrip<jobresult>(); },
    "jobresulteq", [] {
        assert(jobresult::success() == jobresult::success());
        assert(jobresult::failure() == jobresult::failure());
        assert(jobresult::success() != jobresult::failure()); });
