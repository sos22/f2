#include "percentage.H"
#include "test2.H"

#include "parsers.tmpl"
#include "serialise.tmpl"
#include "test2.tmpl"

static testmodule __testpercentage(
    "percentage",
    list<filename>::mk("percentage.C", "percentage.H"),
    testmodule::BranchCoverage(50_pc),
    "serialise", [] {
        quickcheck q;
        serialise<percentage>(q); },
    "parser", [] { parsers::roundtrip(percentage::parser()); },
    "literal", [] {
        assert(10_pc == percentage(10));
        assert(500_pc == percentage(500));
        assert(0.5_pc == percentage(0.5)); });
