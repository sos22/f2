#include "list.H"
#include "test2.H"

#include "test2.tmpl"

static testmodule __listtest(
    "list",
    list<filename>::mk("list.H", "list.tmpl"),
    testmodule::LineCoverage(20_pc),
    testmodule::BranchCoverage(10_pc),
    "pushtail", [] {
        list<int> l;
        l.pushtail(5,6,7,8);
        assert(l.pophead() == 5);
        assert(l.poptail() == 8);
        l.pushtail(9);
        assert(l.pophead() == 6);
        assert(l.pophead() == 7);
        assert(l.pophead() == 9);
        assert(l.empty()); });
