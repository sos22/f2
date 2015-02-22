#include "computeagent.H"

#include "compute.H"
#include "either.H"
#include "eqserver.H"
#include "job.H"
#include "mutex.H"
#include "nnp.H"
#include "rpcservice2.H"
#include "spawn.H"
#include "util.H"

#include "list.tmpl"
#include "rpcservice2.tmpl"
#include "thread.tmpl"

namespace __computeagent {

/* Synchronised by the service lock. */
class runningjob {
public: const job j;
public: const proto::compute::tasktag tag;
public: either<nnp<spawn::process>, orerror<jobresult> > _proc;
public: decltype(_proc) &proc(mutex_t::token) { return _proc; }
public: decltype(_proc) const &proc(mutex_t::token) const { return _proc; }
    /* Only accessed from service thread once constructed. */
public: maybe<spawn::subscription> sub;
public: explicit runningjob(const job &_j,
                            proto::compute::tasktag _tag,
                            spawn::process &__proc,
                            subscriber &subsc)
    : j(_j),
      tag(_tag),
      _proc(Left(), _nnp(__proc)),
      sub(Just(), subsc, __proc) { }
public: jobname name() const { return j.name(); }
public: proto::compute::jobstatus status(mutex_t::token tok) {
    if (proc(tok).isleft()) {
        return proto::compute::jobstatus::running(j.name(),
                                                  tag); }
    else {
        return proto::compute::jobstatus::finished(j.name(),
                                                   tag,
                                                   proc(tok).right()); } }
public: bool running(mutex_t::token tok) const { return proc(tok).isleft(); } };

class computeservice : public rpcservice2 {
private: class maintenancethread : public thread {
    public:  computeservice &owner;
    public:  subscriber sub;
    public:  waitbox<void> shutdown;
    public:  explicit maintenancethread(constoken t, computeservice &_owner)
        : thread(t),
          owner(_owner),
          sub(),
          shutdown() {}
    private: void run(clientio); };
    friend class maintenancethread;
private: mutex_t mux;
private: eqserver &eqs;
private: eventqueue<proto::compute::event> &_eqq;
private: eventqueue<proto::compute::event> &eqq(mutex_t::token) { return _eqq; }
private: list<runningjob> _jobtable;
private: list<runningjob> &jobtable(mutex_t::token) { return _jobtable; }
private: maintenancethread &thr;
private: subscriber sub;
public:  explicit computeservice(const constoken &token,
                                 eqserver &_eqs,
                                 eventqueue<proto::compute::event> &__eqq)
    : rpcservice2(token, mklist(interfacetype::compute, interfacetype::eq)),
      mux(),
      eqs(_eqs),
      _eqq(__eqq),
      _jobtable(),
      thr(*thread::start<maintenancethread>(
              fields::mk("computemaintenance"), *this)),
      sub() {}
public:  static orerror<nnp<computeservice> > build(clientio io,
                                                    const clustername &cn,
                                                    const agentname &sn,
                                                    const filename &config);
public:  orerror<void> called(clientio,
                              deserialise1 &,
                              interfacetype,
                              nnp<incompletecall>,
                              onconnectionthread);
private: orerror<pair<proto::eq::eventid, proto::compute::tasktag> > start(
    const job &,
    const peername &,
    mutex_t::token /* mux */,
    acquirestxlock); };

void
computeservice::maintenancethread::run(clientio io) {
    subscription ss(sub, shutdown.pub);
    while (!shutdown.ready()) {
        auto s(sub.wait(io));
        if (s == &ss) continue;
        /* The synchronisation here is a bit funny. We're the only
         * thing which can mark a job as finished and when we do so it
         * gets removed from our subscriber, so we know that at this
         * point the job is not finished, and the other threads are
         * careful to never release anything which isn't finished.
         * Otherwise, we'd get use-after-frees. */
        auto rj(containerof(s, runningjob, sub.__just()));
        auto muxtoken(owner.mux.lock());
        assert(rj->proc(muxtoken).isleft());
        auto spawntoken(rj->proc(muxtoken).left()->hasdied());
        if (spawntoken == Nothing) {
            /* Not finished yet. */
            owner.mux.unlock(&muxtoken);
            continue; }
        /* Task is done.  Pop it out. */
        rj->sub = Nothing;
        auto res(rj->proc(muxtoken).left()->join(spawntoken.just()));
        if (res.isleft()) {
            if (res.left() == shutdowncode::ok) {
                rj->proc(muxtoken).mkright(jobresult::success()); }
            else {
                rj->proc(muxtoken).mkright(jobresult::failure()); }}
        else {
            /* Process killed by a signal.  Try to guess whether it's
             * their fault or ours. */
            if (res.right().internallygenerated()) {
                rj->proc(muxtoken).mkright(jobresult::failure()); }
            else {
                rj->proc(muxtoken).mkright(error::signalled); } }
        owner.mux.unlock(&muxtoken); } }

orerror<nnp<computeservice> >
computeservice::build(clientio io,
                      const clustername &cn,
                      const agentname &sn,
                      const filename &statefile) {
    auto &eqs(*eqserver::build());
    auto eqq(eqs.openqueue(proto::eq::names::compute, statefile));
    if (eqq.isfailure()) {
        eqs.destroy();
        return eqq.failure(); }
    /* Start the queue with a flushed event, because all jobs have now
     * been lost. */
    eqq.success()->queue(proto::compute::event::flushed(),
                         rpcservice2::acquirestxlock(io));
    return rpcservice2::listen<computeservice>(
        io,
        cn,
        sn,
        peername::all(peername::port::any),
        eqs,
        *eqq.success()); }

orerror<void>
computeservice::called(clientio io,
                       deserialise1 &ds,
                       interfacetype type,
                       nnp<incompletecall> ic,
                       onconnectionthread oct) {
    if (type == interfacetype::eq) return eqs.called(io, ds, ic, oct);
    proto::compute::tag t(ds);
    if (t == proto::compute::tag::start) {
        job j(ds);
        peername p(ds);
        if (ds.isfailure()) return ds.failure();
        auto token(mux.lock());
        auto r(start(j, p, token, acquirestxlock(io)));
        mux.unlock(&token);
        if (r.isfailure()) return r.failure();
        ic->complete(
            [eid = r.success().first(), tag = r.success().second()]
            (serialise1 &s, mutex_t::token /* txlock */, onconnectionthread) {
                s.push(eid);
                s.push(tag); },
            acquirestxlock(io),
            oct);
        return Success; }
    else if (t == proto::compute::tag::enumerate) {
        if (ds.isfailure()) return ds.failure();
        list<proto::compute::jobstatus> res;
        auto tok(mux.lock());
        for (auto it(jobtable(tok).start()); !it.finished(); it.next()) {
            res.append(it->status(tok)); }
        auto eid(eqq(tok).lastid());
        mux.unlock(&tok);
        ic->complete(
            [eid, result = res.steal()]
            (serialise1 &s, mutex_t::token /* txlock */, onconnectionthread) {
                s.push(eid);
                s.push(result); },
            acquirestxlock(io),
            oct);
        return Success; }
    else if (t == proto::compute::tag::drop) {
        jobname jn(ds);
        if (ds.isfailure()) return ds.failure();
        auto tok(mux.lock());
        maybe<decltype(jobtable(tok).start())> job(Nothing);
        for (auto it(jobtable(tok).start());
             job == Nothing && !it.finished();
             it.next()) {
            if (it->name() == jn) job = it; }
        if (job == Nothing) {
            mux.unlock(&tok);
            return error::notfound; }
        else if (job.just()->running(tok)) {
            mux.unlock(&tok);
            return error::toosoon; }
        else {
            job.just().remove();
            mux.unlock(&tok);
            ic->complete(Success, acquirestxlock(io), oct);
            return Success; } }
    else {
        /* Deserialiser shouldn't let us get here. */
        abort(); } }

orerror<pair<proto::eq::eventid, proto::compute::tasktag> >
computeservice::start(
    const job &j,
    const peername &pn,
    mutex_t::token tok,
    acquirestxlock atl) {
    auto r(spawn::process::spawn(
               spawn::program("/bin/echo")
               .addarg("HELLO")
               .addarg(j.field().c_str())
               .addarg(pn.field().c_str())));
    if (r.isfailure()) return r.failure();
    auto tag(proto::compute::tasktag::invent());
    jobtable(tok).append(
        j,
        tag,
        *r.success(),
        thr.sub);
    auto eid(eqq(tok).queue(proto::compute::event::start(j.name(), tag), atl));
    return mkpair(eid, tag); } }

orerror<void>
computeagent::format(const filename &f) {
    return eqserver::formatqueue(proto::eq::names::compute, f); }

orerror<nnp<computeagent> >
computeagent::build(clientio io,
                    const clustername &cn,
                    const agentname &an,
                    const filename &f) {
    auto r(__computeagent::computeservice::build(io, cn, an, f));
    if (r.isfailure()) return r.failure();
    else return _nnp(*(computeagent *)&*r.success()); }
