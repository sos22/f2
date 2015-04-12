#include "percentage.H"

#include "fields.H"
#include "serialise.H"
#include "test.H"

#include "parsers.tmpl"
#include "serialise.tmpl"

percentage::percentage(deserialise1 &ds) : val(ds) {}

void
percentage::serialise(serialise1 &s) const { s.push(val); }

const fields::field &
percentage::field() const { return fields::mk_double(val * 100.0) + "%"; }

const parser<percentage> &
percentage::parser() {
    return (parsers::longdoubleparser + "%").map<percentage>(
        [] (long double x) { return percentage(x); }); }

void
tests::_percentage() {
    testcaseV("percentage", "serialise", [] {
            quickcheck q;
            serialise<percentage>(q); });
    testcaseV("percentage", "parser", [] {
            parsers::roundtrip(percentage::parser()); });
    testcaseV("percentage", "literal", [] {
            assert(10_pc == percentage(10));
            assert(500_pc == percentage(500));
            assert(0.5_pc == percentage(0.5)); }); }
