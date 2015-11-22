#include "test2.H"
#include "waitbox.H"

#include "test2.tmpl"
#include "waitbox.tmpl"

static testmodule __testwaitbox(
    "waitbox",
    list<filename>::mk("waitbox.C", "waitbox.H", "waitbox.tmpl"),
    testmodule::BranchCoverage(45_pc),
    testmodule::LineCoverage(50_pc),
    "setif", [] (clientio io) {
        waitbox<unsigned> wb;
        wb.set(5);
        wb.setif(6);
        wb.setif(7);
        assert(wb.ready());
        assert(wb.get(io) == 5); });
