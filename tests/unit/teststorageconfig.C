#include "storageconfig.H"
#include "test2.H"

#include "parsers.tmpl"
#include "serialise.tmpl"
#include "test2.tmpl"

static testmodule __teststorageconfig(
    "storageconfig",
    list<filename>::mk("storageconfig.C", "storageconfig.H"),
    testmodule::BranchCoverage(50_pc),
    "parsers", [] { parsers::roundtrip(storageconfig::parser()); },
    "serialise", [] {
        quickcheck q;
        serialise<storageconfig>(q); } );
