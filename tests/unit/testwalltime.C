#include "test2.H"
#include "timedelta.H"
#include "walltime.H"

#include "parsers.tmpl"
#include "test2.tmpl"

static testmodule __testwalltime(
    "walltime",
    list<filename>::mk("walltime.C", "walltime.H"),
    testmodule::BranchCoverage(70_pc),
    "granularity", [] (clientio io) {
        auto last(walltime::now());
        for (int i = 0; i < 100; i++) {
            timedelta::microseconds(2).future().sleep(io);
            auto n(walltime::now());
            assert(!(n == last));
            last = n; } },
    "parser", [] { parsers::roundtrip(parsers::_walltime()); },
    "fixedfields", [] {
#define simpletest(val, fmted)                                          \
        assert(!strcmp(fields::mk(walltime::__testmk(val)).c_str(), fmted))
        simpletest(1408854316000123987l, "2014-08-24 04:25:16.000123987");
        simpletest(1208854316000000000l, "2008-04-22 08:51:56.000000000");
        simpletest(2208854316999999999l, "2039-12-30 10:38:36.999999999");
        simpletest(0l, "1970-01-01 00:00:00.000000000");
        simpletest(-1l, "1969-12-31 23:59:59.999999999");
        simpletest(-1000000000l, "1969-12-31 23:59:59.000000000");
        simpletest(-10000000000000000l, "1969-09-07 06:13:20.000000000");
#undef simpletest
        });
