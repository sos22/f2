#include "pair.H"
#include "test2.H"

#include "pair.tmpl"
#include "parsers.tmpl"
#include "test2.tmpl"

#include "fieldfinal.H"

static testmodule __testpair(
    "pair",
    list<filename>::mk("pair.H", "pair.tmpl"),
    testmodule::LineCoverage(50_pc),
    testmodule::BranchCoverage(40_pc),
    "parser", [] {
        parsers::roundtrip<pair<int, int> >();
        parsers::roundtrip<pair<string, string> >(); });
