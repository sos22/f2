#include "fields.H"
#include "pair.H"
#include "string.H"
#include "test.H"

#include "pair.tmpl"
#include "parsers.tmpl"

void
tests::_pair() {
    testcaseV("pair", "parser", [] {
            parsers::roundtrip<pair<int, int> >();
            parsers::roundtrip<pair<string, string> >(); }); }
