#include "logging.H"
#include "test2.H"

#include "test2.tmpl"

static testmodule __testlogging(
    "logging",
    list<filename>::mk("logging.C", "logging.H"),
    testmodule::LineCoverage(70_pc),
    testmodule::BranchCoverage(45_pc),
    /* The logging test is mostly of memlog, which is private to
     * logging.C, so the test itself is also in logging.C */
    "memlog", [] { tests::logging(); });
