#include "jobresult.H"

#include "fields.H"
#include "parsers.H"
#include "serialise.H"

#include "parsers.tmpl"

jobresult::jobresult(bool s) : succeeded(s) {}

jobresult::jobresult(deserialise1 &ds) : succeeded(ds) {}

void
jobresult::serialise(serialise1 &s) const { s.push(succeeded); }

jobresult
jobresult::success() { return jobresult(true); }

jobresult
jobresult::failure() { return jobresult(false); }

const fields::field &
jobresult::field() const {
    if (succeeded) return fields::mk("WIN");
    else return fields::mk("LOSS"); }

const ::parser<jobresult> &
jobresult::parser() {
    return strmatcher("WIN", jobresult::success()) ||
        strmatcher("LOSS", jobresult::failure()); }
