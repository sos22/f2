/* Crash handlers are hard to test, because the interesting bit
 * happens after abort(), which breaks the unit test suite. Work
 * around it by doing almost all the work in a separate shell script
 * and just spawn()ing it from here. */
#include "spawn.H"
#include "test2.H"

#include "test2.tmpl"

static testmodule __testcrashhandler(
    "crashhandler",
    list<filename>::mk("crashhandler.C",
                       "crashhandler.H",
                       "crashhandler.tmpl"),
    testmodule::LineCoverage(18_pc),
    testmodule::BranchCoverage(16_pc),
    testmodule::Dependency("tests/crashhandlers/crasher"),
    "sh", [] (clientio io) {
        auto e(spawn::program("./tests/crashhandlers/crashhandlers.sh")
               .addarg(running_on_valgrind() ? "1" : "20")
               .run(io)
               .fatal("running crashhandlers.sh"));
        /* For some reason doing tassert here makes the linker
         * crash. Oh, the joys of C++. */
        if (e.isright() || e.left() != shutdowncode::ok) {
            error::unknown.fatal("shell script said " + e.field()); } } );
