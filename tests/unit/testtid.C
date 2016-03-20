#include "spark.H"
#include "test2.H"
#include "tid.H"

#include "spark.tmpl"
#include "test2.tmpl"

static testmodule __testtid(
    "tid",
    list<filename>::mk("tid.C", "tid.H"),
    testmodule::BranchCoverage(60_pc),
    "basic", [] {
        assert(tid::me() != tid::nonexistent());
        assert(tid::me() == tid::me());
        assert(!strcmp(tid::me().field().c_str(), tid::me().field().c_str()));},
    "thr", [] {
        spark<tid> t([] { return tid::me(); });
        assert(t.get() != tid::me()); });
