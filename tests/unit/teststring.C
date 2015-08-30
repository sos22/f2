#include "parsers.H"
#include "string.H"
#include "test2.H"

#include "parsers.tmpl"
#include "test2.tmpl"

static testmodule __teststring(
    "string",
    list<filename>::mk("string.C", "string.H"),
    testmodule::LineCoverage(54_pc),
    testmodule::BranchCoverage(31_pc),
    "empty", [] { assert(!strcmp(fields::mk(string("")).c_str(), "\"\"")); },
    "parser", [] { parsers::roundtrip(string::parser()); },
    "concat", [] {
        string x = string("foo") + string("bar");
        assert(x == "foobar");
        x += "bazz";
        assert(x == "foobarbazz"); });
