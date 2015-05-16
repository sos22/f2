#include "job.H"
#include "test2.H"

#include "serialise.tmpl"
#include "test2.tmpl"

static testmodule __testjob(
    "job",
    list<filename>::mk("job.C", "job.H"),
    testmodule::LineCoverage(30_pc),
    testmodule::BranchCoverage(30_pc),
    "serialise", [] {
        quickcheck q;
        serialise<job>(q); },
    "quickchecknodupes", [] {
        quickcheck q;
        unsigned succ(0);
        for (unsigned cntr = 0; succ < 100; cntr++) {
            assert(cntr < 10000);
            deserialise1 ds(q);
            job j(ds);
            if (ds.isfailure()) continue;
            succ++;
            assert(!j.outputs().hasdupes());
            assert(j.outputs().issorted()); } } );
