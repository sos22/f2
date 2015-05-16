#include "storageclient.H"

#include "clientio.H"
#include "connpool.H"
#include "job.H"
#include "nnp.H"
#include "orerror.H"
#include "proto2.H"
#include "serialise.H"
#include "storage.H"
#include "util.H"

#include "connpool.tmpl"
#include "list.tmpl"
#include "orerror.tmpl"

template <typename _resT, typename _implT, typename _innerTokenT> _implT &
storageclient::asynccall<_resT, _implT, _innerTokenT>::impl() {
    return *containerof(this, _implT, api); }

template <typename _resT, typename _implT, typename _innerTokenT> const _implT &
storageclient::asynccall<_resT, _implT, _innerTokenT>::impl() const {
    return *containerof(this, _implT, api); }

template <typename _resT, typename _implT, typename _innerTokenT>
const publisher &
storageclient::asynccall<_resT, _implT, _innerTokenT>::pub() const {
    return impl().cl.pub(); }

template <typename _resT, typename _implT, typename _innerTokenT> orerror<_resT>
storageclient::asynccall<_resT, _implT, _innerTokenT>::pop(clientio io) {
    auto tok(finished());
    if (tok == Nothing) {
        subscriber ss;
        subscription sub(ss, pub());
        tok = finished();
        while (tok == Nothing) {
            ss.wait(io);
            tok = finished(); } }
    return pop(tok.just()); }

template <typename _resT, typename _implT, typename _innerTokenT>
maybe<typename storageclient::asynccall<_resT, _implT, _innerTokenT>::token>
storageclient::asynccall<_resT, _implT, _innerTokenT>::finished() const {
    auto r(impl().cl.finished());
    if (r == Nothing) return Nothing;
    else return token(r.just()); }

template <typename _resT, typename _implT, typename _innerTokenT> void
storageclient::asynccall<_resT, _implT, _innerTokenT>::abort() {
    impl().cl.abort();
    delete &impl(); }

class storageclient::impl {
public: class storageclient api;
public: connpool &cp;
public: const agentname an;
public: impl(connpool &_cp, const agentname &_an) : cp(_cp), an(_an) {} };

class storageclient::impl &
storageclient::impl() { return *containerof(this, class impl, api); }

const class storageclient::impl &
storageclient::impl() const { return *containerof(this, class impl, api); }

class storageclient::asyncconnectimpl {
public: storageclient::asyncconnect api;
public: connpool &cp;
public: const agentname an;
public: connpool::asynccall &cl;
public: asyncconnectimpl(connpool &_cp, const agentname &_an)
    : cp(_cp),
      an(_an),
      cl(*cp.call(an,
                  interfacetype::storage,
                  Nothing,
                  [] (serialise1 &s, connpool::connlock) {
                      s.push(proto::storage::tag::ping); })) {} };

template <> orerror<storageclient::asyncconnect::resT>
storageclient::asyncconnect::pop(token t) {
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
        auto res(new class storageclient::impl(i.cp, i.an));
        delete this;
        return success(_nnp(res->api)); } }

storageclient::asyncconnect &
storageclient::connect(connpool &cp, const agentname &an) {
    return (new asyncconnectimpl(cp, an))->api; }

orerror<storageclient::asyncconnect::resT>
storageclient::connect(clientio io, connpool &cp, const agentname &an) {
    return connect(cp, an).pop(io); }

class storageclient::asynccreatejobimpl {
public: storageclient::asynccreatejob api;
public: connpool::asynccallT<proto::eq::eventid> &cl;
public: explicit asynccreatejobimpl(
    class storageclient::impl &owner,
    const job &j)
    : api(),
      cl(*owner.cp.call<proto::eq::eventid>(
             owner.an,
             interfacetype::storage,
             Nothing,
             [j] (serialise1 &s, connpool::connlock) {
                 s.push(proto::storage::tag::createjob);
                 s.push(j); },
             [] (deserialise1 &ds, connpool::connlock) -> proto::eq::eventid {
                 return proto::eq::eventid(ds); })) {} };

template <> orerror<storageclient::asynccreatejob::resT>
storageclient::asynccreatejob::pop(token t) {
    auto r(impl().cl.pop(t.inner));
    delete &impl();
    return r; }

storageclient::asynccreatejob &
storageclient::createjob(const job &j) {
    return (new asynccreatejobimpl(impl(), j))->api; }

orerror<storageclient::asynccreatejob::resT>
storageclient::createjob(clientio io, const job &j) {
    return createjob(j).pop(io); }

class storageclient::asynclistjobsimpl {
public: storageclient::asynclistjobs api;
public: connpool::asynccallT<proto::storage::listjobsres> &cl;
public: explicit asynclistjobsimpl(class storageclient::impl &owner)
    : api(),
      cl(*owner.cp.call<proto::storage::listjobsres>(
             owner.an,
             interfacetype::storage,
             Nothing,
             [] (serialise1 &s, connpool::connlock) {
                 s.push(proto::storage::tag::listjobs); },
             [] (deserialise1 &ds, connpool::connlock) {
                 return proto::storage::listjobsres(ds); })) {} };

template <> orerror<storageclient::asynclistjobs::resT>
storageclient::asynclistjobs::pop(token t) {
    auto r(impl().cl.pop(t.inner));
    delete &impl();
    orerror<storageclient::asynclistjobs::resT> res(error::unknown);
    if (r.isfailure()) res = r.failure();
    else res.mksuccess(r.success().when, Steal, r.success().res);
    return res; }

storageclient::asynclistjobs &
storageclient::listjobs() { return (new asynclistjobsimpl(impl()))->api; }

orerror<storageclient::asynclistjobs::resT>
storageclient::listjobs(clientio io) { return listjobs().pop(io); }

class storageclient::asyncstatjobimpl {
public: storageclient::asyncstatjob api;
public: connpool::asynccallT<job> &cl;
public: explicit asyncstatjobimpl(class storageclient::impl &owner,
                                  const jobname &jn)
    : api(),
      cl(*owner.cp.call<job>(
             owner.an,
             interfacetype::storage,
             Nothing,
             [jn] (serialise1 &s, connpool::connlock) {
                 s.push(proto::storage::tag::statjob);
                 s.push(jn); },
             [] (deserialise1 &ds, connpool::connlock) {
                 return job(ds); })) {} };

template <> orerror<storageclient::asyncstatjob::resT>
storageclient::asyncstatjob::pop(token t) {
    auto r(impl().cl.pop(t.inner));
    delete &impl();
    orerror<storageclient::asyncstatjob::resT> res(error::unknown);
    if (r.isfailure()) res = r.failure();
    else res = r.success();
    return res; }

storageclient::asyncstatjob &
storageclient::statjob(jobname jn) {
    return (new asyncstatjobimpl(impl(), jn))->api; }

orerror<storageclient::asyncstatjob::resT>
storageclient::statjob(clientio io, jobname jn) { return statjob(jn).pop(io); }

class storageclient::asyncremovejobimpl {
public: storageclient::asyncremovejob api;
public: connpool::asynccall &cl;
public: explicit asyncremovejobimpl(class storageclient::impl &owner,
                                    const jobname &jn)
    : api(),
      cl(*owner.cp.call(
             owner.an,
             interfacetype::storage,
             Nothing,
             [jn] (serialise1 &s, connpool::connlock) {
                 s.push(proto::storage::tag::removejob);
                 s.push(jn); })) {} };

template <> orerror<storageclient::asyncremovejob::resT>
storageclient::asyncremovejob::pop(token t) {
    auto r(impl().cl.pop(t.inner));
    delete &impl();
    return r; }

storageclient::asyncremovejob &
storageclient::removejob(jobname jn) {
    return (new asyncremovejobimpl(impl(), jn))->api; }

orerror<storageclient::asyncremovejob::resT>
storageclient::removejob(clientio io, jobname jn) {
    return removejob(jn).pop(io); }

void
storageclient::destroy() { delete &impl(); }

#define instantiate(name)                                               \
    template class                                                      \
    storageclient::asynccall<storageclient:: name ::resT,               \
                             storageclient:: name ::implT,              \
                             storageclient:: name::innerTokenT>
instantiate(asyncconnect);
instantiate(asynccreatejob);
instantiate(asynclistjobs);
instantiate(asyncstatjob);
instantiate(asyncremovejob);
