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

#include "asynccall.tmpl"
#include "connpool.tmpl"
#include "list.tmpl"
#include "orerror.tmpl"
#include "pair.tmpl"

class storageclient::impl {
public: class storageclient api;
public: connpool &cp;
public: const agentname an;
public: impl(connpool &_cp, const agentname &_an) : cp(_cp), an(_an) {} };

class storageclient::impl &
storageclient::impl() { return *containerof(this, class impl, api); }

const class storageclient::impl &
storageclient::impl() const { return *containerof(this, class impl, api); }

storageclient &
storageclient::connect(connpool &cp, const agentname &an) {
    return (new class impl(cp, an))->api; }

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

class storageclient::asyncappendimpl {
public: storageclient::asyncappend api;
public: connpool::asynccall &cl;
public: explicit asyncappendimpl(class storageclient::impl &owner,
                                 const jobname &jn,
                                 const streamname &sn,
                                 _Steal st,
                                 buffer &b,
                                 bytecount offset)
    : api(),
      cl(*owner.cp.call(
             owner.an,
             interfacetype::storage,
             Nothing,
             [_b = buffer(st, b), jn, offset, sn]
             (serialise1 &s, connpool::connlock) {
                 s.push(proto::storage::tag::append);
                 s.push(jn);
                 s.push(sn);
                 s.push(offset);
                 s.push(_b); })) {}
public: explicit asyncappendimpl(class storageclient::impl &owner,
                                 const jobname &jn,
                                 const streamname &sn,
                                 const buffer &b,
                                 bytecount offset)
    : api(),
      cl(*owner.cp.call(
             owner.an,
             interfacetype::storage,
             Nothing,
             [b, jn, offset, sn]
             (serialise1 &s, connpool::connlock) {
                 s.push(proto::storage::tag::append);
                 s.push(jn);
                 s.push(sn);
                 s.push(offset);
                 s.push(b); })) {} };

template <> orerror<storageclient::asyncappend::resT>
storageclient::asyncappend::pop(token t) {
    auto r(impl().cl.pop(t.inner));
    delete &impl();
    return r; }

storageclient::asyncappend &
storageclient::append(jobname jn,
                      const streamname &sn,
                      const buffer &b,
                      bytecount offset) {
    return (new asyncappendimpl(impl(), jn, sn, b, offset))->api; }

orerror<storageclient::asyncappend::resT>
storageclient::append(clientio io,
                      jobname jn,
                      const streamname &sn,
                      const buffer &b,
                      bytecount offset) {
    return append(jn, sn, b, offset).pop(io); }

storageclient::asyncappend &
storageclient::append(jobname jn,
                      const streamname &sn,
                      _Steal s,
                      buffer &b,
                      bytecount offset) {
    return (new asyncappendimpl(impl(), jn, sn, s, b, offset))->api; }

orerror<storageclient::asyncappend::resT>
storageclient::append(clientio io,
                      jobname jn,
                      const streamname &sn,
                      _Steal s,
                      buffer &b,
                      bytecount offset) {
    return append(jn, sn, s, b, offset).pop(io); }

class storageclient::asyncfinishimpl {
public: storageclient::asyncfinish api;
public: connpool::asynccallT<asyncfinish::resT> &cl;
public: explicit asyncfinishimpl(class storageclient::impl &owner,
                                 jobname jn,
                                 const streamname &sn)
    : api(),
      cl(*owner.cp.call<asyncfinish::resT>(
             owner.an,
             interfacetype::storage,
             Nothing,
             [jn, sn] (serialise1 &s, connpool::connlock) {
                 s.push(proto::storage::tag::finish);
                 s.push(jn);
                 s.push(sn); },
             [] (deserialise1 &ds, connpool::connlock) {
                 return asyncfinish::resT(ds); })) {} };

template <> orerror<storageclient::asyncfinish::resT>
storageclient::asyncfinish::pop(token t) {
    auto r(impl().cl.pop(t.inner));
    delete &impl();
    return r; }

storageclient::asyncfinish &
storageclient::finish(jobname jn, const streamname &sn) {
    return (new asyncfinishimpl(impl(), jn, sn))->api; }

orerror<storageclient::asyncfinish::resT>
storageclient::finish(clientio io, jobname jn, const streamname &sn) {
    return finish(jn, sn).pop(io); }

class storageclient::asyncreadimpl {
public: storageclient::asyncread api;
public: maybe<buffer> res;
public: connpool::asynccallT<bytecount> &cl;
public: explicit asyncreadimpl(class storageclient::impl &owner,
                               jobname jn,
                               const streamname &sn,
                               maybe<bytecount> start,
                               maybe<bytecount> end)
    : api(),
      res(Nothing),
      cl(*owner.cp.call<bytecount>(
             owner.an,
             interfacetype::storage,
             Nothing,
             [end, jn, sn, start] (serialise1 &s, connpool::connlock) {
                 s.push(proto::storage::tag::read);
                 s.push(jn);
                 s.push(sn);
                 s.push(start);
                 s.push(end); },
             [this] (deserialise1 &ds, connpool::connlock) ->orerror<bytecount>{
                 bytecount r1(ds);
                 res.mkjust(ds);
                 if (ds.isfailure()) return ds.failure();
                 else return r1; })) {} };

template <> orerror<storageclient::asyncread::resT>
storageclient::asyncread::pop(token t) {
    auto filesz(impl().cl.pop(t.inner));
    orerror<storageclient::asyncread::resT> res(error::unknown);
    if (filesz.isfailure()) res = filesz.failure();
    else {
        assert(impl().res.isjust());
        res.mksuccess(filesz.success(), Steal, impl().res.just()); }
    delete &impl();
    return res; }

storageclient::asyncread &
storageclient::read(jobname jn,
                    const streamname &sn,
                    maybe<bytecount> start,
                    maybe<bytecount> end) {
    return (new asyncreadimpl(impl(), jn, sn, start, end))->api; }

orerror<storageclient::asyncread::resT>
storageclient::read(clientio io,
                    jobname jn,
                    const streamname &sn,
                    maybe<bytecount> start,
                    maybe<bytecount> end) {
    return read(jn, sn, start, end).pop(io); }


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

class storageclient::asyncliststreamsimpl {
public: storageclient::asyncliststreams api;
public: connpool::asynccallT<proto::storage::liststreamsres> &cl;
public: explicit asyncliststreamsimpl(
    class storageclient::impl &owner,
    jobname jn)
    : api(),
      cl(*owner.cp.call<proto::storage::liststreamsres>(
             owner.an,
             interfacetype::storage,
             Nothing,
             [jn] (serialise1 &s, connpool::connlock) {
                 s.push(proto::storage::tag::liststreams);
                 s.push(jn); },
             [] (deserialise1 &ds, connpool::connlock) {
                 return proto::storage::liststreamsres(ds); })) {} };

template <> orerror<storageclient::asyncliststreams::resT>
storageclient::asyncliststreams::pop(token t) {
    auto r(impl().cl.pop(t.inner));
    delete &impl();
    orerror<storageclient::asyncliststreams::resT> res(error::unknown);
    if (r.isfailure()) res = r.failure();
    else res.mksuccess(r.success().when, Steal, r.success().res);
    return res; }

storageclient::asyncliststreams &
storageclient::liststreams(jobname jn) {
    return (new asyncliststreamsimpl(impl(), jn))->api; }

orerror<storageclient::asyncliststreams::resT>
storageclient::liststreams(clientio io, jobname jn) {
    return liststreams(jn).pop(io); }

class storageclient::asyncstatstreamimpl {
public: storageclient::asyncstatstream api;
public: connpool::asynccallT<streamstatus> &cl;
public: explicit asyncstatstreamimpl(
    class storageclient::impl &owner,
    jobname jn,
    const streamname &sn)
    : api(),
      cl(*owner.cp.call<streamstatus>(
             owner.an,
             interfacetype::storage,
             Nothing,
             [jn, &sn] (serialise1 &s, connpool::connlock) {
                 s.push(proto::storage::tag::statstream);
                 s.push(jn);
                 s.push(sn); },
             [] (deserialise1 &ds, connpool::connlock) {
                 return streamstatus(ds); })) {} };

template <> orerror<storageclient::asyncstatstream::resT>
storageclient::asyncstatstream::pop(token t) {
    auto r(impl().cl.pop(t.inner));
    delete &impl();
    return r; }

storageclient::asyncstatstream &
storageclient::statstream(jobname jn, const streamname &sn) {
    return (new asyncstatstreamimpl(impl(), jn, sn))->api; }

orerror<storageclient::asyncstatstream::resT>
storageclient::statstream(clientio io, jobname jn, const streamname &sn) {
    return statstream(jn, sn).pop(io); }

class storageclient::asyncremovejobimpl {
public: storageclient::asyncremovejob api;
public: connpool::asynccallT<asyncremovejobdescr::_resT> &cl;
public: explicit asyncremovejobimpl(class storageclient::impl &owner,
                                    const jobname &jn)
    : api(),
      cl(*owner.cp.call<asyncremovejobdescr::_resT>(
             owner.an,
             interfacetype::storage,
             Nothing,
             [jn] (serialise1 &s, connpool::connlock) {
                 s.push(proto::storage::tag::removejob);
                 s.push(jn); },
             [] (deserialise1 &ds, connpool::connlock) {
                 return storageclient::asyncremovejob::resT(ds); })) {} };

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

template void asynccall<storageclient::asyncfinishdescr>::abort();
