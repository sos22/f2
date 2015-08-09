#include "computeclient.H"

#include "asynccall.tmpl"
#include "list.tmpl"
#include "maybe.tmpl"
#include "pair.tmpl"

class computeclient::impl {
public: class computeclient api;
public: connpool &cp;
public: const agentname an;
public: impl(connpool &_cp, const agentname &_an) : cp(_cp), an(_an) {} };
class computeclient::impl &
computeclient::impl() { return *containerof(this, class impl, api); }
const class computeclient::impl &
computeclient::impl() const { return *containerof(this, class impl, api); }

computeclient &
computeclient::connect(connpool &cp, const agentname &an) {
    return (new class impl(cp, an))->api; }

class computeclient::enumerateimpl {
public: computeclient::asyncenumerate api;
public: maybe<pair<proto::eq::eventid, list<proto::compute::jobstatus> > > res;
public: connpool::asynccall &cl;
public: enumerateimpl(class computeclient::impl &owner)
    : api(),
      res(Nothing),
      cl(*owner.cp.call(
             owner.an,
             interfacetype::compute,
             Nothing,
             [] (serialise1 &s, connpool::connlock) {
                 s.push(proto::compute::tag::enumerate); },
             [this] (deserialise1 &ds, connpool::connlock) -> orerror<void> {
                 res.mkjust(ds);
                 return ds.status(); })) {} };
template <> orerror<computeclient::enumeratedescr::_resT>
computeclient::asyncenumerate::pop(token t) {
    auto r(impl().cl.pop(t.inner));
    orerror<computeclient::enumeratedescr::_resT> rv(error::unknown);
    if (r.isfailure()) rv = r.failure();
    else rv.mksuccess(impl().res.just(), Steal);
    delete &impl();
    return rv; }
computeclient::asyncenumerate &
computeclient::enumerate() { return (new enumerateimpl(impl()))->api; }
orerror<computeclient::enumeratedescr::_resT>
computeclient::enumerate(clientio io) { return enumerate().pop(io); }

class computeclient::startimpl {
public: computeclient::asyncstart api;
public: maybe<startdescr::_resT> res;
public: connpool::asynccall &cl;
public: startimpl(class computeclient::impl &owner, const job &j)
    : api(),
      res(Nothing),
      cl(*owner.cp.call(
             owner.an,
             interfacetype::compute,
             Nothing,
             [j] (serialise1 &s, connpool::connlock) {
                 s.push(proto::compute::tag::start);
                 s.push(j); },
             [this] (deserialise1 &ds, connpool::connlock) -> orerror<void> {
                 res.mkjust(ds);
                 return ds.status(); })) {} };
template <> orerror<computeclient::startdescr::_resT>
computeclient::asyncstart::pop(token t) {
    auto r(impl().cl.pop(t.inner));
    orerror<computeclient::startdescr::_resT> rv(error::unknown);
    if (r.isfailure()) rv = r.failure();
    else rv.mksuccess(impl().res.just());
    delete &impl();
    return rv; }
computeclient::asyncstart &
computeclient::start(const job &j) { return (new startimpl(impl(), j))->api; }
orerror<computeclient::startdescr::_resT>
computeclient::start(clientio io, const job &j) { return start(j).pop(io); }

class computeclient::dropimpl {
public: computeclient::asyncdrop api;
public: connpool::asynccall &cl;
public: dropimpl(class computeclient::impl &owner, const jobname &j)
    : api(),
      cl(*owner.cp.call(
             owner.an,
             interfacetype::compute,
             Nothing,
             [j] (serialise1 &s, connpool::connlock) {
                 s.push(proto::compute::tag::drop);
                 s.push(j); })) {} };
template <> orerror<computeclient::dropdescr::_resT>
computeclient::asyncdrop::pop(token t) {
    auto r(impl().cl.pop(t.inner));
    delete &impl();
    return r; }
computeclient::asyncdrop &
computeclient::drop(const jobname &j) { return (new dropimpl(impl(), j))->api; }
orerror<computeclient::dropdescr::_resT>
computeclient::drop(clientio io, const jobname &j) { return drop(j).pop(io); }

void
computeclient::destroy() { delete &impl(); }
