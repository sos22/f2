#include "job.H"

#include "fields.H"
#include "filename.H"
#include "jobname.H"
#include "parsers.H"
#include "serialise.H"
#include "streamname.H"

#include "list.tmpl"
#include "map.tmpl"
#include "pair.tmpl"
#include "parsers.tmpl"
#include "serialise.tmpl"

#include "fieldfinal.H"

job::job(const filename &_library,
         const string &_function,
         const map<streamname, inputsrc> &_inputs,
         const list<streamname> &_outputs)
    : library(_library),
      function(_function),
      inputs(_inputs),
      outputs(_outputs) {}

void
job::serialise(serialise1 &s) const {
    s.push(library);
    s.push(function);
    s.push(inputs);
    s.push(outputs); }

job::job(deserialise1 &ds)
    : library(ds),
      function(ds),
      inputs(ds),
      outputs(ds) {}

jobname
job::name() const { return jobname(digest(fields::mk(*this))); }

const fields::field &
job::field() const { return fields::mk(*this); }

const fields::field &
fields::mk(const job &j) {
    auto acc(&("<job:" + mk(j.library) + ":"+ mk(j.function)));
    for (auto it(j.inputs.start()); !it.finished(); it.next()) {
        acc = &(*acc + " -<" + mk(it.key()) + ":" + mk(it.value())); }
    for (auto it(j.outputs.start()); !it.finished(); it.next()) {
        acc = &(*acc + " ->" + mk(*it)); }
    return *acc + ">"; }

const parser<job> &
job::parser() {
    auto &stream(
        ("->" + streamname::parser() + ":" + job::inputsrc::parser()) |
        ("-<" + streamname::parser()));
    return ("<job:" + filename::parser() + ":" + string::parser() +
            ~strmatcher(" ") + parsers::sepby(stream, strmatcher(" ")) +
            ">")
        .maperr<job>(
            [] (const pair<pair<pair<filename, string>,
                                maybe<void> >,
                           list<either<pair<streamname, job::inputsrc>,
                                       streamname> > > &what) {
                orerror<job> res(
                    Success,
                    what.first().first().first(),
                    what.first().first().second(),
                    map<streamname, inputsrc>(),
                    list<streamname>());
                for (auto it(what.second().start()); !it.finished(); it.next()){
                    if (it->isleft()) {
                        if (res.success().inputs.get(it->left().first())
                                != Nothing) {
                            res = error::duplicate;
                            return res; }
                        res.success().inputs.set(it->left().first(),
                                                 it->left().second()); }
                    else res.success().outputs.append(it->right()); }
                return res; }); }
