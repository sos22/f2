#include "jobname.H"

#include "either.H"
#include "fields.H"
#include "parsers.H"
#include "string.H"

#include "parsers.tmpl"

jobname::jobname(deserialise1 &ds) : d(ds) {}

void
jobname::serialise(serialise1 &s) const { d.serialise(s); }

const fields::field &
fields::mk(const jobname &jn) { return "<job:" + mk(jn.d) + ">"; }

string
jobname::asfilename() const { return d.denseprintable(); }

const parser< ::jobname> &
parsers::_jobname() {
    return (("<job:" + _digest() + ">") || _digest())
        .map<jobname>([] (const digest &d) {
                return jobname(d); }); }
