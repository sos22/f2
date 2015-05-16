#include "streamname.H"
#include "test2.H"

#include "test2.tmpl"

static const testmodule __teststreamname(
    "streamname",
    list<filename>::mk("streamname.C", "streamname.H"),
    testmodule::BranchCoverage(50_pc),
    testmodule::LineCoverage(40_pc),
    "quickcheck", [] {
        /* Quickcheck should generate a valid name reasonably
         * quickly. */
        quickcheck q;
        deserialise1 ds(q);
        for (unsigned cntr = 0; true; cntr++) {
            streamname s(ds);
            if (!ds.isfailure()) break;
            assert(cntr < 10); } });
