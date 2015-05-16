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
        serialise<job>(q); });
