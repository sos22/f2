#include "jobname.H"

#include "either.H"
#include "parsers.H"
#include "string.H"

#include "either.tmpl"
#include "parsers.tmpl"
#include "wireproto.tmpl"

wireproto_simple_wrapper_type(jobname, digest, d);

jobname::jobname(deserialise1 &ds) : d(ds) {}

void
jobname::serialise(serialise1 &s) const { d.serialise(s); }

const fields::field &
fields::mk(const jobname &jn) {
    return "<job:" + mk(jn.d) + ">"; }

string
jobname::asfilename() const {
    return d.denseprintable(); }

const parser< ::jobname> &
parsers::_jobname() {
    return (("<job:" + _digest() + ">") || _digest())
        .map<jobname>([] (const digest &d) {
                return jobname(d); }); }

bool
jobname::operator<(const jobname &o) const {
    return d < o.d; }

bool
jobname::operator>(const jobname &o) const {
    return d > o.d; }
