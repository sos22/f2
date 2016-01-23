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

void
job::serialise(serialise1 &s) const {
    s.push(library);
    s.push(function);
    s.push(inputs);
    s.push(_outputs);
    s.push(immediate); }

job::job(deserialise1 &ds)
    : library(ds),
      function(ds),
      inputs(ds),
      _outputs(ds),
      immediate(ds) {
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

job::job(const filename &_library,
         const string &_function,
         const map<streamname, inputsrc> &_inputs,
         const list<streamname> &__outputs,
         const map<string, string> &_immediate)
    : library(_library),
      function(_function),
      inputs(_inputs),
      _outputs(__outputs),
      immediate(_immediate) {
      sort(_outputs); }

job::job(const filename &_library,
         const string &_function)
    : library(_library),
      function(_function),
      inputs(empty),
      _outputs(empty),
      immediate(empty) {}

job &
job::addoutput(const streamname &sn) {
    _outputs.append(sn);
    sort(_outputs);
    return *this; }

job &
job::addinput(const streamname &inpname,
              const jobname &jn,
              const streamname &outname) {
    inputs.set(inpname, inputsrc(jn, outname));
    return *this; }

job &
job::addimmediate(const string &key, const string &val) {
    immediate.set(key, val);
    return *this; }

jobname
job::name() const { return jobname(digest(fields::mk(*this))); }

bool
job::operator==(const job &o) const {
    return library == o.library &&
        function == o.function &&
        inputs == o.inputs &&
        outputs() == o.outputs() &&
        immediate == o.immediate; }

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
    list<pair<string, string> > imm;
    for (auto it(immediate.start()); !it.finished(); it.next()) {
        imm.append(it.key(), it.value()); }
    sort(imm);
    for (auto it(imm.start()); !it.finished(); it.next()) {
        acc = &(*acc + " " + it->first().field() +"="+ it->second().field()); }
    return *acc + ">"; }

const parser<job> &
job::parser() {
    auto &stream(("-<" + streamname::parser() + ":" + job::inputsrc::parser()) |
                 ("->" + streamname::parser()));
    auto &imm(string::parser() + "=" + string::parser());
    auto &parameter(stream|imm);
    auto &hdr("<job:" + filename::parser() + ":" + string::parser() +
              ~strmatcher(" "));
    auto &inner(hdr + parsers::sepby(parameter, strmatcher(" ")) + ">");
    class f : public ::parser<job> {
    public: decltype(inner) &_inner;
    public: f(decltype(_inner) &__inner) : _inner(__inner) {}
    public: orerror<result> parse(const char *what) const {
        auto r(_inner.parse(what));
        if (r.isfailure()) return r.failure();
        auto &_hdr(r.success().res.first());
        orerror<result> _res(
            Success,
            r.success().left,
            _hdr.first().first(),
            _hdr.first().second());
        auto &res(_res.success().res);
        auto &_parameters(r.success().res.second());
        for (auto it(_parameters.start()); !it.finished(); it.next()) {
            if (it->isleft()) {
                auto &_stream(it->left());
                if (_stream.isleft()) {
                    auto &input(_stream.left());
                    if (res.inputs.haskey(input.first())) {
                        _res = error::duplicate;
                        return _res; }
                    res.inputs.set(input.first(), input.second()); }
                else res._outputs.append(_stream.right()); }
            else {
                auto &_imm(it->right());
                if (res.immediate.haskey(_imm.first())) {
                    _res = error::duplicate;
                    return _res; }
                res.immediate.set(_imm.first(), _imm.second()); } }
        sort(res._outputs);
        if (res.outputs().hasdupes()) _res = error::noparse;
        return _res; } };
    return *new f(inner); }

template class map<streamname, pair<jobname, streamname> >;
