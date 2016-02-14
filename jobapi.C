#include "jobapi.H"
#include "jobapiimpl.H"

#include "job.H"
#include "storageclient.H"
#include "util.H"

#include "either.tmpl"
#include "map.tmpl"
#include "maybe.tmpl"
#include "orerror.tmpl"
#include "pair.tmpl"

namespace {
class outputstreamimpl : public jobapi::outputstream {
public: jobapi::impl &owner;
public: const streamname sn;
public: bytecount expsize;
public: outputstreamimpl(jobapi::impl &_owner,
                         const streamname &_sn)
    : owner(_owner),
      sn(_sn),
      expsize(0_B) {}
public: void append(clientio io, const buffer &) final; };

class inputstreamimpl : public jobapi::inputstream {
public: jobapi::impl &owner;
public: const jobname jn;
public: const streamname sn;
public: inputstreamimpl(jobapi::impl &_owner,
                        const jobname &_jn,
                        const streamname &_sn)
    : owner(_owner),
      jn(_jn),
      sn(_sn) {}
public: buffer read(clientio io, maybe<bytecount>, maybe<bytecount>) final; }; }

class jobapi::impl {
public: jobapi api;
public: storageclient &sc;
public: const job self;
public: map<streamname, outputstreamimpl> outputs;
public: map<streamname, inputstreamimpl> inputs;
public: explicit impl(storageclient &_sc, const job &_self)
    : api(),
      sc(_sc),
      self(_self),
      outputs(),
      inputs() {
    logmsg(loglevel::info, "job is " + self.field()); } };

jobapi::impl &
jobapi::implementation() { return *containerof(this, impl, api); }

const jobapi::impl &
jobapi::implementation() const { return *containerof(this, impl, api); }

jobapi::jobapi() {}

jobapi::~jobapi() {}

const map<string, string> &
jobapi::immediate() const {
    logmsg(loglevel::info, "immediate args " + implementation().self.immediate.field());
    return implementation().self.immediate; }

maybe<nnp<jobapi::outputstream> >
jobapi::output(const streamname &sn) {
    auto &i(implementation());
    auto e(i.outputs.getptr(sn));
    if (e == NULL) {
        if (!i.self.outputs().contains(sn)) return Nothing;
        e = &i.outputs.set(sn, i, sn); }
    return _nnp(*static_cast<outputstream *>(e)); }

maybe<nnp<jobapi::inputstream> >
jobapi::input(const streamname &sn) {
    auto &i(implementation());
    auto e(i.inputs.getptr(sn));
    if (e == NULL) {
        auto r(i.self.inputs.get(sn));
        if (r == Nothing) return Nothing;
        e = &i.inputs.set(sn, i, r.just().first(), r.just().second()); }
    return _nnp(*static_cast<inputstream *>(e)); }

jobapi &
newjobapi(storageclient &sc, const job &self) {
    return (new jobapi::impl(sc, self))->api; }

void
deletejobapi(jobapi &api) { delete containerof(&api, jobapi::impl, api); }

void
outputstreamimpl::append(clientio io, const buffer &b) {
    owner.sc.append(io,
                    owner.self.name(),
                    sn,
                    b,
                    expsize)
        .fatal("appending to output");
    expsize += bytecount::bytes(b.avail()); }

buffer
inputstreamimpl::read(clientio io,
                      maybe<bytecount> start,
                      maybe<bytecount> end) {
    return owner.sc.read(io,
                         jn,
                         sn,
                         start,
                         end)
        .fatal("reading from input")
        .second(); }
