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
    auto acc(&("<job:" + mk(j.library) + ":"+ mk(j.function)));
    for (auto it(j.outputs.start()); !it.finished(); it.next()) {
        acc = &(*acc + " ->" + mk(*it)); }
    return *acc + ">"; }

const parser<job> &
parsers::_job() {
    auto &output("->" + streamname::parser());
    return ("<job:" + filename::parser() + ":" + string::parser() +
            ~strmatcher(" ") + parsers::sepby(output, strmatcher(" ")) +
            ">")
        .map<job>(
            [] (const pair<pair<pair<filename, string>,
                                maybe<void> >,
                           list<streamname> > &what) {
                return job(what.first().first().first(),
                           what.first().first().second(),
                           what.second()); }); }
