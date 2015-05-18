#include "parsers.H"
#include "string.H"
#include "test2.H"

#include "parsers.tmpl"
#include "test2.tmpl"

static testmodule __teststring(
    "string",
    list<filename>::mk("string.C", "string.H"),
    testmodule::LineCoverage(25_pc),
    testmodule::BranchCoverage(17_pc),
    "empty", [] { assert(!strcmp(fields::mk(string("")).c_str(), "\"\"")); },
    "parser", [] { parsers::roundtrip(string::parser()); });
