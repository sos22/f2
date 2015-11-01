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
#include "testassert.H"
#include "test2.H"

#include "either.tmpl"
#include "parsers.tmpl"
#include "testassert.tmpl"
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
      computeformatres(::computeagent::format(computedir).warn("computedir")),
      storagedir(q),
      storageformatres(::storageagent::format(storagedir).warn("storagedir")),
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
                       "runjob.C",
                       "jobtest.C"),
    testmodule::LineCoverage(85_pc),
    testmodule::BranchCoverage(60_pc),
    testmodule::Dependency("jobtest.so"),
    testmodule::Dependency("runjob" EXESUFFIX),
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
        job j(filename("./jobtest.so"), "testfunction");
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
        job j(filename("./jobtest.so"), "waitonesecond");
        t.createjob(io, j);
        assert(t.cc.waitjob(io, j.name()) == error::toosoon);
        t.cc.start(io, j).fatal("starting job");
        auto taken(timedelta::time([&] {
                    assert(t.cc.waitjob(io, j.name())
                           .fatal("waitjob")
                           .fatal("waitjob inner")
                           .issuccess()); }));
        assert(taken > 900_ms);
        assert(taken < 1500_ms);
        t.cc.drop(io, j.name()).fatal("dropjob");
        assert(t.cc.waitjob(io, j.name()) == error::toosoon); },
    "abortjob", [] (clientio io) {
        computetest t(io);
        job j(filename("./jobtest.so"), "waitforever");
        t.createjob(io, j);
        t.cc.start(io, j).fatal("starting job");
        tassert(T2(timedelta,
                   timedelta::time([&] { t.computeagent->destroy(io); })) <
                T(100_ms));
        t.computeagent = NULL; },
    "dropbad", [] (clientio io) {
        computetest t(io);
        job j(filename("./jobtest.so"), "waitforever");
        t.createjob(io, j);
        assert(t.cc.drop(io, j.name()) == error::notfound);
        t.cc.start(io, j).fatal("starting job");
        assert(t.cc.drop(io, j.name()) == error::toosoon); },
    "finishstreams", [] (clientio io) {
        computetest t(io);
        auto ss(streamname::mk("outstream").fatal("mkstreamname"));
        auto j(job(filename("./jobtest.so"), "testfunction").addoutput(ss));
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
    "helloworld", [] (clientio io) {
        computetest t(io);
        auto ss(streamname::mk("output").fatal("output name"));
        auto j(job(filename("./jobtest.so"), "helloworld").addoutput(ss));
        t.createjob(io, j);
        t.cc.start(io, j).fatal("starting job");
        assert(t.cc.waitjob(io, j.name())
               .flatten()
               .fatal("waitjob")
               .issuccess());
        assert(t.sc.statstream(io, j.name(), ss)
               .fatal("statstream2")
               .isfinished());
        auto r(t.sc.read(io, j.name(), ss).fatal("read"));
        assert(r.second().contenteq(buffer("Hello world")));
        assert(r.first() == 11_B); },
    "runjob", [] (clientio io) {
        computetest t(io);
        auto ss(streamname::mk("output").fatal("output name"));
        auto j(job(filename("./jobtest.so"), "helloworld").addoutput(ss));
        t.createjob(io, j);
        assert(t.cc
               .runjob(io, j)
               .fatal("running job")
               .fatal("job failed")
               .issuccess());
        assert(t.sc.statstream(io, j.name(), ss)
               .fatal("statstream2")
               .isfinished());
        auto r(t.sc.read(io, j.name(), ss).fatal("read"));
        assert(r.second().contenteq(buffer("Hello world")));
        assert(r.first() == 11_B); },
    "runbad", [] (clientio io) {
        computetest t(io);
        job j(filename("doesntexist"), "bad");
        assert(t.cc.runjob(io, j).success() == error::toosoon); },
    "runbad2", [] (clientio io) {
        computetest t(io);
        job j(filename("doesntexist"), "bad");
        t.createjob(io, j);
        assert(t.cc.runjob(io, j).success() == error::dlopen); },
    "helloinput", [] (clientio io) {
        computetest t(io);
        auto ss(streamname::mk("output").fatal("output name"));
        auto j(job(filename("./jobtest.so"), "helloworld").addoutput(ss));
        t.sc.createjob(io, j).fatal("creating storage for job");
        /* Do it all through the storage client, not the compute
         * agent, so that the tests are independent. */
        t.sc.append(io, j.name(), ss, buffer("Hello world"), 0_B)
            .fatal("append");
        auto evt1(t.sc.finish(io, j.name(), ss)
                  .fatal("finish"));
        /* Now create a job to consume it. */
        auto sss(streamname::mk("input").fatal("input name"));
        auto jj(job(filename("./jobtest.so"),
                    "helloinput")
                .addinput(sss, j.name(), ss));
        auto evt2(t.sc.createjob(io, jj).fatal("creating second job"));
        t.fsc.storagebarrier(io, t.storageagentname, evt1)
            .fatal("synchronising finish to filesystem");
        t.fsc.storagebarrier(io, t.storageagentname, evt2)
            .fatal("synchronising create2 to filesystem");
        t.cc.start(io, jj).fatal("starting job");
        assert(t.cc.waitjob(io, jj.name())
               .flatten()
               .fatal("waitjob")
               .issuccess()); },
    "helloinputfail", [] (clientio io) {
        computetest t(io);
        auto ss(streamname::mk("output").fatal("output name"));
        auto j(job(filename("./jobtest.so"),
                   "helloworld")
               .addoutput(ss));
        t.sc.createjob(io, j).fatal("creating storage for job");
        t.sc.append(io, j.name(), ss, buffer("Wrong world"), 0_B)
            .fatal("append");
        t.fsc.storagebarrier(
            io,
            t.storageagentname,
            t.sc.finish(io, j.name(), ss)
                .fatal("finish")).
            fatal("synchronise finish to filesystem");
        auto sss(streamname::mk("input").fatal("input name"));
        auto jj(job(filename("./jobtest.so"),
                    "helloinput")
                .addinput(sss, j.name(), ss));
        t.fsc.storagebarrier(
            io,
            t.storageagentname,
            t.sc.createjob(io, jj).fatal("creating second job"))
            .fatal("synchronise create job 2 to filesystem");
        t.cc.start(io, jj).fatal("starting job");
        assert(t.cc.waitjob(io, jj.name())
               .flatten()
               .fatal("waitjob")
               .isfailure()); },
    "immediate", [] (clientio io) {
        computetest t(io);
        auto ss(streamname::mk("output").fatal("output name"));
        auto j(job(filename("./jobtest.so"), "echo")
               .addoutput(ss)
               .addimmediate("val", "hello"));
        t.createjob(io, j);
        t.cc.runjob(io, j).flatten().fatal("running echo");
        auto r(t.sc.read(io, j.name(), ss).fatal("read"));
        assert(r.second().contenteq(buffer("hello")));
        assert(r.first() == 5_B); },
    "abortwaitjob", [] (clientio io) {
        computetest t(io);
        job j(filename("./jobtest.so"), "waitforever");
        t.createjob(io, j);
        t.cc.start(io, j).fatal("starting job");
        for (unsigned x = 0; x < 100; x++) {
            auto &a(t.cc.waitjob(j.name()));
            timedelta::milliseconds(random()%5).future().sleep(io);
            a.abort(); } },
    "abortwaitjob2", [] (clientio io) {
        computetest t(io);
        job j("./jobtest.so", "testfunction");
        auto j2(job("./jobtest.so", "testfunction")
                .addimmediate("ignore", "just for diffing"));
        t.createjob(io, j);
        t.createjob(io, j2);
        for (unsigned x = 0; x < 100; x++) {
            t.cc.start(io, j).fatal("starting job");
            t.cc.start(io, j2).fatal("starting job2");
            auto &a(t.cc.waitjob(j.name()));
            timedelta::milliseconds(random()%5).future().sleep(io);
            a.abort();
            t.cc.waitjob(io, j.name()).fatal("waiting for real");
            t.cc.drop(io, j.name()).fatal("dropping job");
            t.cc.waitjob(io, j2.name()).fatal("waiting for real 2");
            t.cc.drop(io, j2.name()).fatal("dropping job2"); } },
    "starttwice", [] (clientio io) {
        computetest t(io);
        auto ss(streamname::mk("output").fatal("output name"));
        auto j(job("./jobtest.so", "helloworld").addoutput(ss));
        t.createjob(io, j);
        t.cc.start(io, j).fatal("starting job1");
        tassert(T(t.cc.start(io, j)) == T(error::toolate));
        t.cc.waitjob(io, j.name()).fatal("waiting for job1");
        tassert(T(t.cc.start(io, j)) == T(error::toolate));
        t.cc.drop(io, j.name()).fatal("dropping finished job");
        t.cc.start(io, j).fatal("starting job2"); },
    "jobresultparse", [] { parsers::roundtrip<jobresult>(); },
    "jobresulteq", [] {
        assert(jobresult::success() == jobresult::success());
        assert(jobresult::failure() == jobresult::failure());
        assert(jobresult::success() != jobresult::failure()); });
