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
               .isfailure()); },
    "badstdout", [] (clientio io) {
        computetest ct(io);
        auto j(job(JOBEXEC, "exec")
               .addimmediate("program", "/bin/echo")
               .addimmediate("arg0", "goes nowhere"));
        ct.createjob(io, j);
        ct.cc.start(io, j).fatal("starting job");
        assert(ct.cc.waitjob(io, j.name())
               .fatal("waitjob")
               .fatal("waitjob inner")
               .isfailure()); },
    "stdout", [] (clientio io) {
        computetest ct(io);
        auto sn(streamname::mk("stdout").fatal("stdout"));
        const char *str = "goes somewhere";
        auto j(job(JOBEXEC, "exec")
               .addimmediate("program", "/bin/echo")
               .addimmediate("arg0", str)
               .addoutput(sn));
        ct.createjob(io, j);
        ct.cc.start(io, j).fatal("starting job");
        assert(ct.cc.waitjob(io, j.name())
               .fatal("waitjob")
               .fatal("waitjob inner")
               .issuccess());
        auto b(ct.sc.read(io, j.name(), sn)
               .fatal("reading stdout")
               .second());
        buffer bb;
        bb.queue(str, strlen(str));
        bb.queue("\n", 1);
        assert(b.contenteq(bb)); } );
