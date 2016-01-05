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
    testmodule::LineCoverage(19_pc),
    testmodule::BranchCoverage(14_pc),
    testmodule::Dependency("tests/crashhandlers/crasher"),
    "sh", [] (clientio io) {
        auto e(spawn::program("./tests/crashhandlers/crashhandlers.sh")
               .run(io)
               .fatal("running crashhandlers.sh"));
        assert(e.isleft());
        assert(e.left() == shutdowncode::ok); });
