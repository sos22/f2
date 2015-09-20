#include "percentage.H"

#include "fields.H"
#include "serialise.H"

#include "parsers.tmpl"

percentage::percentage(deserialise1 &ds) : val(ds) {}

void
percentage::serialise(serialise1 &s) const { s.push(val); }

const fields::field &
percentage::field() const { return fields::mk_double(val) + "%"; }

const parser<percentage> &
percentage::parser() {
    return (parsers::longdoubleparser + "%").map<percentage>(
        [] (long double x) { return percentage(x); }); }
