#include "parsers.H"
#include "string.H"
#include "test2.H"

#include "parsers.tmpl"
#include "test2.tmpl"

static testmodule __teststring(
    "string",
    list<filename>::mk("string.C", "string.H"),
    testmodule::LineCoverage(60_pc),
    testmodule::BranchCoverage(36_pc),
    "empty", [] { assert(!strcmp(fields::mk(string("")).c_str(), "\"\"")); },
    "parser", [] { parsers::roundtrip(string::parser()); },
    "prefix", [] {
        assert(string("aaaa").stripprefix("bbbbb") == Nothing);
        assert(string("aaaa").stripprefix("") == "aaaa");
        assert(string("aaaa").stripprefix("a") == "aaa");
        assert(string("abc").stripprefix("") == "abc"); },
    "concat", [] {
        string x = string("foo") + string("bar");
        assert(x == "foobar");
        x += "bazz";
        assert(x == "foobarbazz"); });
