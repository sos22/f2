#include "jobapi.H"
#include "jobapiimpl.H"

#include "job.H"
#include "storageclient.H"
#include "util.H"

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
public: void append(clientio io, const buffer &) final; }; }

class jobapi::impl {
public: jobapi api;
public: storageclient &sc;
public: const job self;
public: explicit impl(storageclient &_sc, const job &_self)
    : api(),
      sc(_sc),
      self(_self) {} };

jobapi::impl &
jobapi::implementation() { return *containerof(this, impl, api); }

jobapi::jobapi() {}

jobapi::~jobapi() {}

maybe<nnp<jobapi::outputstream> >
jobapi::output(const streamname &sn) {
    if (!implementation().self.outputs().contains(sn)) return Nothing;
    return _nnp(*static_cast<jobapi::outputstream *>(
                    new outputstreamimpl(implementation(), sn))); }

maybe<nnp<jobapi::inputstream> >
jobapi::input(const streamname &) { return Nothing; }

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
