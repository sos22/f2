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
#include "maybe.tmpl"
#include "rpcservice2.tmpl"
#include "thread.tmpl"

namespace __computeagent {

/* Synchronised by the service lock. */
class runningjob : public thread {
public: const job j;
public: const proto::compute::tasktag tag;
    /* Connects main maintenance thread to our death publisher. */
public: maybe<subscription> subsc;
    /* Set by the thread as it shutds down. */
public: maybe<orerror<jobresult> > result;
public: explicit runningjob(const constoken &token,
                            const job &_j,
                            proto::compute::tasktag _tag,
                            subscriber &sub)
    : thread(token),
      j(_j),
      tag(_tag),
      subsc(Just(), sub, pub()),
      result(Nothing) {}
public: void run(clientio); };

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
private: list<runningjob *> _runningjobs;
private: list<runningjob *> &runningjobs(mutex_t::token) {
    return _runningjobs; }
private: list<proto::compute::jobstatus> _finishedjobs;
private: list<proto::compute::jobstatus> &finishedjobs(mutex_t::token) {
    return _finishedjobs; }
private: maintenancethread &thr;
private: subscriber sub;
public:  explicit computeservice(const constoken &token,
                                 eqserver &_eqs,
                                 eventqueue<proto::compute::event> &__eqq)
    : rpcservice2(token, mklist(interfacetype::compute, interfacetype::eq)),
      mux(),
      eqs(_eqs),
      _eqq(__eqq),
      _runningjobs(),
      _finishedjobs(),
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
    mutex_t::token /* mux */,
    acquirestxlock); };

void
runningjob::run(clientio io) {
    auto s(spawn::process::spawn(
               spawn::program("/bin/echo")
               .addarg("HELLO")
               .addarg(j.field().c_str())));
    if (s.isfailure()) {
        result.mkjust(s.failure());
        return; }
    /* XXX there should be some way of aborting these. */
    auto res(s.success()->join(io));
    if (res.isleft()) {
        if (res.left() == shutdowncode::ok) result.mkjust(jobresult::success());
        else result.mkjust(jobresult::failure()); }
    else {
        /* Process killed by a signal.  Try to guess whether it's
         * their fault or ours. */
        if (res.right().internallygenerated()) {
            result.mkjust(jobresult::failure()); }
        else result.mkjust(error::signalled); } }

void
computeservice::maintenancethread::run(clientio io) {
    subscription ss(sub, shutdown.pub);
    while (!shutdown.ready()) {
        auto s(sub.wait(io));
        if (s == &ss) continue;
        auto rj(containerof(s, runningjob, subsc.__just()));
        auto threadtoken(rj->hasdied());
        if (threadtoken == Nothing) continue;
        rj->subsc = Nothing;
        assert(rj->result != Nothing);
        auto muxtoken(owner.mux.lock());
        auto &f(owner.finishedjobs(muxtoken).append(
                    proto::compute::jobstatus::finished(
                        rj->j.name(), rj->tag, rj->result.just())));
        owner.runningjobs(muxtoken).drop(rj);
        rj->join(threadtoken.just());
        owner.eqq(muxtoken).queue(
            proto::compute::event::finish(f),
            rpcservice2::acquirestxlock(io));
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
        if (ds.isfailure()) return ds.failure();
        auto token(mux.lock());
        auto r(start(j, token, acquirestxlock(io)));
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
        for (auto it(runningjobs(tok).start()); !it.finished(); it.next()) {
            res.append(proto::compute::jobstatus::running((*it)->j.name(),
                                                          (*it)->tag)); }
        for (auto it(finishedjobs(tok).start()); !it.finished(); it.next()) {
            res.append(*it); }
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
        for (auto job(finishedjobs(tok).start()); !job.finished(); job.next()) {
            if (job->name == jn) {
                eqq(tok).queue(
                    proto::compute::event::removed(*job),
                    acquirestxlock(io));
                job.remove();
                mux.unlock(&tok);
                ic->complete(Success, acquirestxlock(io), oct);
                return Success; } }
        /* It's not in the finished table.  Check for running tasks.
         * Only really needed to give better error messages. */
        bool running = false;
        for (auto it(runningjobs(tok).start());
             !running && !it.finished();
             it.next()) {
            if ((*it)->j.name() == jn) {
                mux.unlock(&tok);
                return error::toosoon; } }
        /* It isn't anywhere. */
        mux.unlock(&tok);
        return error::notfound; }
    else {
        /* Deserialiser shouldn't let us get here. */
        abort(); } }

orerror<pair<proto::eq::eventid, proto::compute::tasktag> >
computeservice::start(
    const job &j,
    mutex_t::token tok,
    acquirestxlock atl) {
    auto tag(proto::compute::tasktag::invent());
    runningjobs(tok).pushtail(
        thread::start<runningjob>(
            "J" + fields::mk(j.name()),
            j,
            tag,
            thr.sub));
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
