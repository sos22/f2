#include "tests/lib/testctxt.H"
#include "test2.H"
#include "testassert.H"

#include "orerror.tmpl"
#include "test2.tmpl"
#include "testassert.tmpl"

static testmodule __testjob(
    "jobexec",
    list<filename>::mk("jobexec.C"),
#define JOBEXEC "./jobexec" EXESUFFIX ".so"
    testmodule::Dependency(JOBEXEC),
    testmodule::Dependency("runjob" EXESUFFIX),
    testmodule::Dependency("tests/abort/abort"),
    testmodule::BranchCoverage(70_pc),
    testmodule::LineCoverage(95_pc),
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
    "sleep", [] (clientio io) {
        computetest ct(io);
        auto j(job(JOBEXEC, "exec")
               .addimmediate("program", "/bin/sleep")
               .addimmediate("arg0", "1"));
        ct.createjob(io, j);
        auto start(timestamp::now());
        ct.cc.start(io, j).fatal("starting job");
        assert(ct.cc.waitjob(io, j.name())
               .fatal("waitjob")
               .fatal("waitjob inner")
               .issuccess());
        auto end(timestamp::now());
        tassert(T(end) - T(start) >= T(1_s / (running_on_valgrind()
                                              ? VALGRIND_TIMEWARP
                                              : 1)));
        tassert(T(end) - T(start) < T(2_s)); },
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
    "missingarg", [] (clientio io) {
        computetest ct(io);
        auto j(job(JOBEXEC, "exec")
               .addimmediate("program", "/bin/true")
               .addimmediate("arg1", "missing0"));
        ct.createjob(io, j);
        ct.cc.start(io, j).fatal("starting job");
        /* Returning error::unknown here isn't really ideal, but then
         * neither is anything else. It isn't strictly incorrect, at
         * any rate. */
        assert(ct.cc.waitjob(io, j.name())
               .fatal("waitjob") == error::unknown); },
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
    "slowbadstdout", [] (clientio io) {
        computetest ct(io);
        auto j(job(JOBEXEC, "exec")
               .addimmediate("program", "/bin/sh")
               .addimmediate("arg0", "-c")
               .addimmediate("arg1", "echo foo; sleep 3600"));
        ct.createjob(io, j);
        ct.cc.start(io, j).fatal("starting job");
        auto t(timedelta::time([&] {
                    assert(ct.cc.waitjob(io, j.name())
                           .fatal("waitjob")
                           .fatal("waitjob inner")
                           .isfailure()); } ));
        tassert(T(t) < T(5_s)); },
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
        assert(b.contenteq(bb)); },
    "stderr", [] (clientio io) {
        computetest ct(io);
        auto sn(streamname::mk("stderr").fatal("stderr"));
        auto j(job(JOBEXEC, "exec")
               .addimmediate("program", "/bin/sh")
               .addimmediate("arg0", "-c")
               .addimmediate("arg1", "echo foo >&2")
               .addoutput(sn));
        ct.createjob(io, j);
        ct.cc.start(io, j).fatal("starting job");
        assert(ct.cc.waitjob(io, j.name())
               .fatal("waitjob")
               .fatal("waitjob inner")
               .issuccess());
        auto b(ct.sc.read(io, j.name(), sn)
               .fatal("reading stderr")
               .second());
        buffer bb;
        bb.queue("foo\n", 4);
        assert(b.contenteq(bb)); },
    "catempty", [] (clientio io) {
        computetest ct(io);
        auto sn(streamname::mk("stdout").fatal("stdout"));
        auto j(job(JOBEXEC, "exec")
               .addimmediate("program", "/bin/cat")
               .addoutput(sn));
        ct.createjob(io, j);
        ct.cc.start(io, j).fatal("starting job");
        assert(ct.cc.waitjob(io, j.name())
               .fatal("waitjob")
               .fatal("waitjob inner")
               .issuccess());
        tassert(T(ct.sc.read(io, j.name(), sn)
                  .fatal("reading stdout")
                  .first())
                == T(0_B)); },
    "bigstdout", [] (clientio io) {
        computetest ct(io);
        auto sn(streamname::mk("stdout").fatal("stdout"));
        auto j(job(JOBEXEC, "exec")
               .addimmediate("program", "/bin/sh")
               .addimmediate("arg0", "-c")
               .addimmediate(
                   "arg1",
                   "dd if=/dev/urandom bs=4k count=256 2> /dev/null")
               .addoutput(sn));
        ct.createjob(io, j);
        ct.cc.start(io, j).fatal("starting job");
        assert(ct.cc.waitjob(io, j.name())
               .fatal("waitjob")
               .fatal("waitjob inner")
               .issuccess());
        auto b(ct.sc.read(io, j.name(), sn, 0_B, 100_B)
               .fatal("reading stdout"));
        tassert(T(b.second().avail()) == T(100u));
        tassert(T(b.first()) == T(1_MiB)); },
    "stdin", [] (clientio io) {
        computetest ct(io);
        auto s1(streamname::mk("s1").fatal("s1"));
        auto inp(job("dummy", "dummy").addoutput(s1));
        ct.createjob(io, inp);
        ct.filloutput(io, inp.name(), s1, "hello");
        auto j(job(JOBEXEC, "exec")
               .addimmediate("program", "/bin/sh")
               .addimmediate("arg0", "-c")
               .addimmediate("arg1", "read inp; [ $inp = hello ]")
               .addinput(streamname::mk("stdin").fatal("stdin"),
                         inp.name(),
                         s1));
        ct.createjob(io, j);
        ct.cc.start(io, j).fatal("starting job");
        assert(ct.cc.waitjob(io, j.name())
               .fatal("waitjob")
               .fatal("waitjob inner")
               .issuccess()); },
    "stdinslow", [] (clientio io) {
        computetest ct(io);
        auto s1(streamname::mk("s1").fatal("s1"));
        auto inp(job("dummy", "dummy").addoutput(s1));
        ct.createjob(io, inp);
        ct.filloutput(io, inp.name(), s1, "hello");
        auto j(job(JOBEXEC, "exec")
               .addimmediate("program", "/bin/sh")
               .addimmediate("arg0", "-c")
               .addimmediate("arg1",
                             "read inp; sleep 1; [ $inp = hello ]")
               .addinput(streamname::mk("stdin").fatal("stdin"),
                         inp.name(),
                         s1));
        ct.createjob(io, j);
        ct.cc.start(io, j).fatal("starting job");
        assert(ct.cc.waitjob(io, j.name())
               .fatal("waitjob")
               .fatal("waitjob inner")
               .issuccess()); },
    "catbig", [] (clientio io) {
        computetest ct(io);
        auto s1(streamname::mk("s1").fatal("s1"));
        auto s2(streamname::mk("stdout").fatal("s2"));
        auto inp(job("dummy", "dummy").addoutput(s1));
        auto inpn(inp.name());
        ct.createjob(io, inp);
        for (unsigned x = 0; x < 1000; x++) {
            buffer b;
            char buf[4096];
            memset(buf, x, sizeof(buf));
            b.queue(buf, sizeof(buf));
            ct.sc.append(io, inpn, s1, b, 4096_B * x)
                .fatal("append " + fields::mk(x)); }
        ct.sc.finish(io, inp.name(), s1).fatal("finishing output");
        auto j(job(JOBEXEC, "exec")
               .addimmediate("program", "/bin/cat")
               .addinput(streamname::mk("stdin").fatal("stdin"), inpn, s1)
               .addoutput(s2));
        ct.createjob(io, j);
        ct.cc.start(io, j).fatal("starting job");
        assert(ct.cc.waitjob(io, j.name())
               .fatal("waitjob")
               .fatal("waitjob inner")
               .issuccess());
        for (unsigned x = 0; x < 1000; x++) {
            auto r(ct.sc.read(io, j.name(), s2, 4096_B*x, 4096_B*(x+1))
                   .fatal("read " + fields::mk(x)));
            tassert(T(r.first()) == T(4096_B * 1000));
            buffer b;
            char buf[4096];
            memset(buf, x, sizeof(buf));
            b.queue(buf, sizeof(buf));
            assert(r.second().contenteq(b)); } }
 );
