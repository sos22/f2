#include "backtrace.H"
#include "logging.H"
#include "test2.H"

#include "test2.tmpl"

/* Not, if we're being honest, a particularly useful test case, beyond
 * confirming that it won't crash in at least one easy case. */
static const testmodule __testbacktrace(
    "backtrace",
    list<filename>::mk("backtrace.C", "backtrace.H"),
    testmodule::BranchCoverage(60_pc),
    "null", [] { logmsg(loglevel::info, backtrace().field()); } );
