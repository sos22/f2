#include "tests/lib/testctxt.H"
#include "test2.H"

#include "orerror.tmpl"
#include "test2.tmpl"

static testmodule __testjob(
    "jobexec",
    list<filename>::mk("jobexec.C"),
#define JOBEXEC "./jobexec" EXESUFFIX ".so"
    testmodule::Dependency(JOBEXEC),
    testmodule::Dependency("runjob" EXESUFFIX),
    testmodule::Dependency("tests/abort/abort"),
    testmodule::BranchCoverage(35_pc),
    testmodule::LineCoverage(50_pc),
    "true", [] (clientio io) {
        computetest ct(io);
        auto j(job(JOBEXEC, "exec")
               .addimmediate("program", "/bin/true"));
        logmsg(loglevel::info, "job is " + j.field());
        ct.createjob(io, j);
        ct.cc.start(io, j).fatal("starting job");
        assert(ct.cc.waitjob(io, j.name())
               .fatal("waitjob")
               .fatal("waitjob inner")
               .issuccess()); },
    "false", [] (clientio io) {
        computetest ct(io);
        auto j(job(JOBEXEC, "exec")
               .addimmediate("program", "/bin/false"));
        ct.createjob(io, j);
        ct.cc.start(io, j).fatal("starting job");
        assert(ct.cc.waitjob(io, j.name())
               .fatal("waitjob")
               .fatal("waitjob inner")
               .isfailure()); },
    "abort", [] (clientio io) {
        computetest ct(io);
        auto j(job(JOBEXEC, "exec")
               .addimmediate("program", "./tests/abort/abort"));
        ct.createjob(io, j);
        ct.cc.start(io, j).fatal("starting job");
        assert(ct.cc.waitjob(io, j.name())
               .fatal("waitjob")
               .fatal("waitjob inner")
               .isfailure()); });
