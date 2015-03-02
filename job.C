#include "job.H"

#include "fields.H"
#include "filename.H"
#include "jobname.H"
#include "parsers.H"
#include "serialise.H"

#include "parsers.tmpl"

job::job(const filename &l, const string &f) : library(l), function(f) {}

void
job::serialise(serialise1 &s) const {
    s.push(library);
    s.push(function); }

job::job(deserialise1 &ds)
    : library(ds),
      function(ds) {}

jobname
job::name() const { return jobname(digest(fields::mk(*this))); }

const fields::field &
job::field() const { return fields::mk(*this); }

const fields::field &
fields::mk(const job &j) {
    return "<job:" + mk(j.library) + ":"+mk(j.function)+">"; }

const parser<job> &
parsers::_job() {
    return ("<job:" + _filename() + ":" + strparser +">")
        .map<job>(
            [] (const pair<filename, const char *> &what) {
                return job(what.first(), string(what.second())); }); }
