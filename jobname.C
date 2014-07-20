#include "jobname.H"

#include "parsers.H"
#include "string.H"

#include "parsers.tmpl"
#include "wireproto.tmpl"

wireproto_simple_wrapper_type(jobname, digest, d);

const fields::field &
fields::mk(const jobname &jn) {
    return "<job:" + mk(jn.d) + ">"; }

string
jobname::asfilename() const {
    return d.denseprintable(); }

const parser< ::jobname> &
parsers::_jobname() {
    return ("<job:" + _digest() + ">")
        .map<jobname>([] (const digest &d) {
                return jobname(d); }); }
