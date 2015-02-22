#include "job.H"

#include "fields.H"
#include "jobname.H"
#include "parsers.H"
#include "serialise.H"

#include "parsers.tmpl"

job::job(const string &s) : message(s) {}

void
job::serialise(serialise1 &s) const {
    s.push(message); }

job::job(deserialise1 &ds)
    : message(ds) {}

jobname
job::name() const { return jobname(digest(fields::mk(*this))); }

const fields::field &
job::field() const { return fields::mk(*this); }

const fields::field &
fields::mk(const job &j) {
    return "<job:" + mk(j.message) + ">"; }

const parser<job> &
parsers::_job() {
    return "<job:" +
        strparser.map<job>(
            [] (const char *const&what) { return job(string(what)); }) +
        ">"; }
