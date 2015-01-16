#include "fields.H"
#include "string.H"
#include "test.H"

void
tests::_string(void) {
    testcaseV("string", "empty", [] {
            assert(!strcmp(fields::mk(string("")).c_str(), "")); }); }
