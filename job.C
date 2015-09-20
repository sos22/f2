#include "job.H"

#include "fields.H"
#include "filename.H"
#include "jobname.H"
#include "parsers.H"
#include "serialise.H"
#include "streamname.H"

#include "fields.tmpl"
#include "list.tmpl"
#include "map.tmpl"
#include "pair.tmpl"
#include "parsers.tmpl"
#include "serialise.tmpl"

job::job(const filename &_library,
         const string &_function,
         const map<streamname, inputsrc> &_inputs,
         const list<streamname> &__outputs)
    : library(_library),
      function(_function),
      inputs(_inputs),
      _outputs(__outputs) {
      sort(_outputs); }

void
job::serialise(serialise1 &s) const {
    s.push(library);
    s.push(function);
    s.push(inputs);
    s.push(_outputs); }

job::job(deserialise1 &ds)
    : library(ds),
      function(ds),
      inputs(ds),
      _outputs(ds) {
    if (ds.random()) {
        sort(_outputs);
        auto it(_outputs.start());
        if (it.finished()) return;
        auto itnext(it);
        itnext.next();
        while (!itnext.finished()) {
            if (*it == *itnext) itnext.remove();
            else {
                it.next();
                itnext.next(); } }
        return; }
    if (outputs().hasdupes() || !outputs().issorted()) {
        ds.fail(error::invalidmessage);
        _outputs.flush(); } }

jobname
job::name() const { return jobname(digest(fields::mk(*this))); }

bool
job::operator==(const job &o) const {
    return library == o.library &&
        function == o.function &&
        inputs == o.inputs &&
        outputs() == o.outputs(); }

const fields::field &
job::field() const {
    assert(outputs().issorted());
    auto acc(&("<job:" + library.field() + ":"+ function.field()));
    list<string> sortinput;
    for (auto it(inputs.start()); !it.finished(); it.next()) {
        sortinput.append((" -<" + it.key().field() + ":" + it.value().field())
                      .c_str()); }
    sort(sortinput);
    for (auto it(sortinput.start()); !it.finished(); it.next()) {
        acc = &(*acc + fields::mk(it->c_str())); }
    for (auto it(outputs().start()); !it.finished(); it.next()) {
        acc = &(*acc + " ->" + it->field()); }
    return *acc + ">"; }

const parser<job> &
job::parser() {
    auto &stream(
        ("-<" + streamname::parser() + ":" + job::inputsrc::parser()) |
        ("->" + streamname::parser()));
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
                    else res.success()._outputs.append(it->right()); }
                sort(res.success()._outputs);
                if (res.success().outputs().hasdupes()) {
                    res = error::noparse; }
                return res; }); }
