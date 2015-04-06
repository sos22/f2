#include "fields.H"
#include "parsers.H"
#include "string.H"
#include "test.H"

#include "parsers.tmpl"

void
tests::_string(void) {
    testcaseV("string", "empty", [] {
            assert(!strcmp(fields::mk(string("")).c_str(), "")); });
    testcaseV("string", "parser", [] {
            parsers::roundtrip(string::parser()); }); }
