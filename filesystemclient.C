#include "filesystemclient.H"

#include "filesystemproto.H"
#include "jobname.H"
#include "serialise.H"

#include "asynccall.tmpl"
#include "connpool.tmpl"
#include "list.tmpl"
#include "orerror.tmpl"

class filesystemclient::impl {
public: class filesystemclient api;
public: connpool &cp;
public: const agentname an;
public: impl(connpool &_cp, const agentname &_an) : cp(_cp), an(_an) {} };
class filesystemclient::impl &
filesystemclient::impl() { return *containerof(this, class impl, api); }
const class filesystemclient::impl &
filesystemclient::impl() const { return *containerof(this, class impl, api); }

filesystemclient &
filesystemclient::connect(connpool &cp, const agentname &an) {
    return (new class impl(cp, an))->api; }

const agentname &
filesystemclient::name() const { return impl().an; }

class filesystemclient::asyncfindjobimpl {
public: filesystemclient::asyncfindjob api;
public: maybe<list<agentname> > res;
public: connpool::asynccall &cl;
public: asyncfindjobimpl(class filesystemclient::impl &owner,
                         const jobname &jn)
    : api(),
      res(Nothing),
      cl(*owner.cp.call<void>(
             owner.an,
             interfacetype::filesystem,
             Nothing,
             [jn] (serialise1 &s, connpool::connlock) {
                 s.push(proto::filesystem::tag::findjob);
                 s.push(jn); },
             [this] (deserialise1 &ds, connpool::connlock) -> orerror<void> {
                 res.mkjust(ds);
                 return ds.status(); })) {} };
template <> orerror<list<agentname> >
filesystemclient::asyncfindjob::pop(token t) {
    auto r(impl().cl.pop(t.inner));
    orerror<list<agentname> > rv(error::unknown);
    if (r.isfailure()) rv = r.failure();
    else rv.mksuccess(Steal, impl().res.just());
    delete &impl();
    return rv; }
filesystemclient::asyncfindjob &
filesystemclient::findjob(jobname jn) {
    return (new asyncfindjobimpl(impl(), jn))->api; }
orerror<filesystemclient::asyncfindjob::resT>
filesystemclient::findjob(clientio io, jobname j) {
    return findjob(j).pop(io); }

class filesystemclient::asyncfindstreamimpl {
public: asyncfindstream api;
public: maybe<asyncfindstream::resT> res;
public: connpool::asynccall &cl;
public: asyncfindstreamimpl(class filesystemclient::impl &owner,
                            const jobname &jn,
                            const streamname &sn)
    : api(),
      res(Nothing),
      cl(*owner.cp.call<void>(
             owner.an,
             interfacetype::filesystem,
             Nothing,
             [jn, sn] (serialise1 &s, connpool::connlock) {
                 s.push(proto::filesystem::tag::findstream);
                 s.push(jn);
                 s.push(sn); },
             [this] (deserialise1 &ds, connpool::connlock) -> orerror<void> {
                 res.mkjust(ds);
                 return ds.status(); })) {} };
template <> orerror<filesystemclient::asyncfindstream::resT>
filesystemclient::asyncfindstream::pop(token t) {
    auto r(impl().cl.pop(t.inner));
    orerror<resT> rv(error::unknown);
    if (r.isfailure()) rv = r.failure();
    else rv.mksuccess(Steal, impl().res.just());
    delete &impl();
    return rv; }
filesystemclient::asyncfindstream &
filesystemclient::findstream(jobname jn, const streamname &sn) {
    return (new asyncfindstreamimpl(impl(), jn, sn))->api; }
orerror<filesystemclient::asyncfindstream::resT>
filesystemclient::findstream(clientio io, jobname j, const streamname &sn) {
    return findstream(j, sn).pop(io); }

class filesystemclient::asyncstoragebarrierimpl {
public: filesystemclient::asyncstoragebarrier api;
public: connpool::asynccallT<void> &cl;
public: asyncstoragebarrierimpl(class filesystemclient::impl &owner,
                                const agentname &an,
                                proto::eq::eventid eid)
    : api(),
      cl(*owner.cp.call(
             owner.an,
             interfacetype::filesystem,
             Nothing,
             [an, eid]
             (serialise1 &s, connpool::connlock) {
                 s.push(proto::filesystem::tag::storagebarrier);
                 s.push(an);
                 s.push(eid); })) {} };
template <> orerror<filesystemclient::asyncstoragebarrierdescr::_resT>
filesystemclient::asyncstoragebarrier::pop(token t) {
    auto res(impl().cl.pop(t.inner));
    delete this;
    return res; }
filesystemclient::asyncstoragebarrier &
filesystemclient::storagebarrier(const agentname &an, proto::eq::eventid eid) {
    return (new asyncstoragebarrierimpl(impl(), an, eid))->api; }
orerror<filesystemclient::asyncstoragebarrier::resT>
filesystemclient::storagebarrier(clientio io,
                                 const agentname &an,
                                 proto::eq::eventid eid) {
    return storagebarrier(an, eid).pop(io); }

void
filesystemclient::destroy() { delete &impl(); }

template const publisher &filesystemclient::asyncfindjob::pub() const;
template maybe<filesystemclient::asyncfindjob::token>
    filesystemclient::asyncfindjob::finished() const;
template void filesystemclient::asyncfindjob::abort();
template class asynccall<filesystemclient::asyncstoragebarrierdescr>;
