#include "job.H"

#include "fields.H"
#include "filename.H"
#include "jobname.H"
#include "parsers.H"
#include "serialise.H"

#include "list.tmpl"
#include "parsers.tmpl"

#include "fieldfinal.H"

job::job(const filename &_library,
         const string &_function,
         const list<streamname> &_outputs)
    : library(_library),
      function(_function),
      outputs(_outputs) {}

void
job::serialise(serialise1 &s) const {
    s.push(library);
    s.push(function);
    s.push(outputs); }

job::job(deserialise1 &ds)
    : library(ds),
      function(ds),
      outputs(ds) {}

jobname
job::name() const { return jobname(digest(fields::mk(*this))); }

const fields::field &
job::field() const { return fields::mk(*this); }

const fields::field &
fields::mk(const job &j) {
    auto acc(&("<job:" + mk(j.library) + ":"+mk(j.function)));
    if (!j.outputs.empty()) acc = &(*acc + " ->" + mk(j.outputs));
    return *acc + ">"; }

const parser<job> &
parsers::_job() {
    return ("<job:" + _filename() + ":" + strparser +
            ~(" ->" + list<streamname>::parse(parsers::_streamname())) + ">")
        .map<job>(
            [] (const pair<pair<filename, const char *>,
                           maybe<list<streamname> > > &what) {
                list<streamname> empty;
                return job(what.first().first(),
                           string(what.first().second()),
                           what.second().dflt(empty)); }); }
