#include "filesystemclient.H"

#include "filesystemproto.H"
#include "jobname.H"
#include "serialise.H"

#include "asynccall.tmpl"
#include "connpool.tmpl"
#include "list.tmpl"

class filesystemclient::impl {
public: class filesystemclient api;
public: connpool &cp;
public: const agentname an;
public: impl(connpool &_cp, const agentname &_an) : cp(_cp), an(_an) {} };
class filesystemclient::impl &
filesystemclient::impl() { return *containerof(this, class impl, api); }
const class filesystemclient::impl &
filesystemclient::impl() const { return *containerof(this, class impl, api); }

class filesystemclient::asyncconnectimpl {
public: filesystemclient::asyncconnect api;
public: connpool &cp;
public: const agentname an;
public: connpool::asynccall &cl;
public: asyncconnectimpl(connpool &_cp, const agentname &_an)
    : cp(_cp),
      an(_an),
      cl(*cp.call(an,
                  interfacetype::filesystem,
                  Nothing,
                  [] (serialise1 &s, connpool::connlock) {
                      s.push(proto::filesystem::tag::ping); })) {} };
template <> orerror<filesystemclient::asyncconnect::resT>
filesystemclient::asyncconnect::pop(token t) {
    auto &i(impl());
    auto r(i.cl.pop(t.inner));
    if (r.isfailure()) {
        logmsg(
            loglevel::failure,
            "failed to connect to " +i.an.field() + ": " + r.failure().field());
        delete this;
        return r.failure(); }
    else {
        logmsg(
            loglevel::debug,
            "connected to " + i.an.field());
        auto res(new class filesystemclient::impl(i.cp, i.an));
        delete this;
        return success(_nnp(res->api)); } }
filesystemclient::asyncconnect &
filesystemclient::connect(connpool &cp, const agentname &an) {
    return (new asyncconnectimpl(cp, an))->api; }
orerror<filesystemclient::asyncconnect::resT>
filesystemclient::connect(clientio io, connpool &cp, const agentname &an) {
    return connect(cp, an).pop(io); }

class filesystemclient::asyncfindjobimpl {
public: filesystemclient::asyncfindjob api;
public: maybe<list<agentname> > res;
public: connpool::asynccall &cl;
public: asyncfindjobimpl(class filesystemclient::impl &owner,
                         const jobname &jn)
    : api(),
      res(Nothing),
      cl(*owner.cp.call(
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
    return impl().cl.pop(t.inner); }
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
