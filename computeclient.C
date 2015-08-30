#include "computeclient.H"

#include "eqclient.H"
#include "thread.H"

#include "asynccall.tmpl"
#include "list.tmpl"
#include "maybe.tmpl"
#include "orerror.tmpl"
#include "pair.tmpl"
#include "thread.tmpl"
#include "waitbox.tmpl"

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
      cl(*owner.cp.call<void>(
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
      cl(*owner.cp.call<void>(
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

class computeclient::asyncwaitjob::impl : public thread {
public: asyncwaitjob api;
public: waitbox<void> abort;
public: waitbox<orerror<orerror<jobresult> > > res;
public: class computeclient::impl &owner;
public: jobname jn;
public: orerror<nnp<eqclient<proto::compute::event> > > connectEQ(clientio io) {
    subscriber sub;
    subscription abortedsub(sub, abort.pub());
    auto &eqcc(*eqclient<proto::compute::event>::connect(
                   owner.cp,
                   owner.an,
                   proto::eq::names::compute));
    {   subscription eqccsub(sub, eqcc.pub());
        while (abort.poll() == Nothing && eqcc.finished() == Nothing) {
            sub.wait(io); } }
    if (abort.poll() != Nothing) {
        eqcc.abort();
        return error::aborted; }
    else {
        auto r(eqcc.pop(eqcc.finished().just()));
        if (r.isfailure()) return r.failure();
        else return r.success().first(); } }
public: orerror<maybe<orerror<jobresult> > > alreadyFinished(clientio io) {
    subscriber sub;
    subscription abortedsub(sub, abort.pub());
    auto &enumcall(owner.api.enumerate());
    {   subscription enumcallsub(sub, enumcall.pub());
        while (abort.poll() == Nothing && enumcall.finished() == Nothing) {
            sub.wait(io); } }
    if (abort.poll() != Nothing) {
        enumcall.abort();
        return error::aborted; }
    auto enm(enumcall.pop(enumcall.finished().just()));
    if (enm.isfailure()) return enm.failure();
    for (auto it(enm.success().second().start()); !it.finished(); it.next()) {
        if (it->name == jn) return it->result; }
    /* The job hasn't been started, so there's no point waiting for
     * it. */
    return error::toosoon; }
public: orerror<orerror<jobresult> > watcheq(
    eqclient<proto::compute::event> &eqcc,
    clientio io) {
    subscriber sub;
    subscription abortedsub(sub, abort.pub());
    subscription eqsub(sub, eqcc.pub());
    while (true) {
        auto r(eqcc.popid());
        if (r == Nothing) {
            if (abort.poll() != Nothing) return error::aborted;
            sub.wait(io);
            continue; }
        if (r.just().isfailure()) return r.just().failure();
        auto &evt(r.just().success().second());
        if (evt.finish() == Nothing ||
            evt.finish().just().first() != jn) {
            continue; }
        return evt.finish().just().second().first(); } }
public: void run(clientio io) {
    /* Connect to event queue */
    auto _eqcc(connectEQ(io));
    if (_eqcc.isfailure()) {
        res.set(_eqcc.failure());
        return; }
    auto &eqcc(*_eqcc.success());
    /* Check for completion before we connected. */
    {   auto r(alreadyFinished(io));
        if (r.isfailure() || r.success() != Nothing) {
            eqcc.destroy();
            if (r.isfailure()) res.set(r.failure());
            else res.set(r.success().just());
            return; } }
    /* Watch the event queue for completions. */
    auto r(watcheq(eqcc, io));
    eqcc.destroy();
    /* If we dropped events out of the queue then restart from the
     * beginning. */
    if (r == error::eventsdropped) return run(io);
    /* Otherwise, the result we get from the queue is the final
     * result. */
    res.set(r); }
public: impl(constoken ct,
             jobname _jn,
             class computeclient::impl &_owner)
    : thread(ct),
      api(),
      abort(),
      res(),
      owner(_owner),
      jn(_jn) {} };

const class computeclient::asyncwaitjob::impl &
computeclient::asyncwaitjob::impl() const {
    return *containerof(this, class impl, api); }
class computeclient::asyncwaitjob::impl &
computeclient::asyncwaitjob::impl() {
    return *containerof(this, class impl, api); }

maybe<computeclient::asyncwaitjob::token>
computeclient::asyncwaitjob::finished() const {
    if (impl().res.ready()) return token();
    else return Nothing; }

const publisher &
computeclient::asyncwaitjob::pub() const { return impl().res.pub(); }

orerror<orerror<jobresult> >
computeclient::asyncwaitjob::pop(token) {
    /* tokens are supposed to enforce this. */
    assert(impl().res.ready());
    auto res(impl().res.get(Steal, clientio::CLIENTIO));
    impl().join(clientio::CLIENTIO);
    return res; }

void
computeclient::asyncwaitjob::abort() {
    impl().abort.set();
    /* Guaranteed quick because the abort box is set. */
    impl().join(clientio::CLIENTIO); }

orerror<orerror<jobresult> >
computeclient::asyncwaitjob::pop(clientio io) {
    subscriber sub;
    {   subscription ss(sub, pub());
        while (finished() == Nothing) sub.wait(io); }
    return pop(finished().just()); }

computeclient::asyncwaitjob &
computeclient::waitjob(jobname jn) {
    return thread::start<class asyncwaitjob::impl>(
        fields::mk("asyncwaitjob"),
        jn,
        impl())->api; }

orerror<orerror<jobresult> >
computeclient::waitjob(clientio io, jobname jn) {
    return waitjob(jn).pop(io); }

void
computeclient::destroy() { delete &impl(); }
