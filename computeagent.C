#include "computeagent.H"

#include <dlfcn.h>

#include "connpool.H"
#include "compute.H"
#include "either.H"
#include "eqserver.H"
#include "filesystemclient.H"
#include "job.H"
#include "jobapi.H"
#include "jobapiimpl.H"
#include "logging.H"
#include "mutex.H"
#include "nnp.H"
#include "rpcservice2.H"
#include "spawn.H"
#include "storageclient.H"
#include "util.H"

#include "either.tmpl"
#include "fd.tmpl"
#include "list.tmpl"
#include "map.tmpl"
#include "maybe.tmpl"
#include "orerror.tmpl"
#include "pair.tmpl"
#include "rpcservice2.tmpl"
#include "thread.tmpl"
#include "waitbox.tmpl"

namespace __computeagent {

class computeservice;

/* Synchronised by the service lock. */
class runningjob : public thread {
public: computeservice &owner;
public: const job j;
public: const proto::compute::tasktag tag;
    /* Connects main maintenance thread to our death publisher. */
public: maybe<subscription> subsc;
    /* Set by the thread as it shuts down. */
public: maybe<orerror<jobresult> > result;
    /* Set by the maintenance thread when it wants us to shut down. */
public: waitbox<void> &shutdown;
public: explicit runningjob(const constoken &token,
                            computeservice &_owner,
                            const job &_j,
                            proto::compute::tasktag _tag,
                            waitbox<void> &_shutdown,
                            subscriber &sub)
    : thread(token),
      owner(_owner),
      j(_j),
      tag(_tag),
      subsc(Just(), sub, pub()),
      result(Nothing),
      shutdown(_shutdown) {}
public: void run(clientio); };

class computeservice : public rpcservice2 {
private: class maintenancethread : public thread {
    public:  computeservice &owner;
    public:  subscriber sub;
    public:  explicit maintenancethread(constoken t, computeservice &_owner)
        : thread(t),
          owner(_owner),
          sub() {}
    private: void run(clientio); };
public:  connpool &cp;
public:  filesystemclient &fs;
private: mutex_t mux;
private: eqserver &eqs;
public:  waitbox<void> shutdown;
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
                                 connpool &_cp,
                                 const agentname &_fs,
                                 eqserver &_eqs,
                                 eventqueue<proto::compute::event> &__eqq)
    : rpcservice2(token, mklist(interfacetype::compute, interfacetype::eq)),
      cp(_cp),
      fs(filesystemclient::connect(cp, _fs)),
      mux(),
      eqs(_eqs),
      shutdown(),
      _eqq(__eqq),
      _runningjobs(),
      _finishedjobs(),
      thr(*thread::start<maintenancethread>(
              fields::mk("computemaintenance"), *this)),
      sub() {}
public:  static orerror<nnp<computeservice> > build(clientio io,
                                                    const clustername &cn,
                                                    const agentname &fs,
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
    acquirestxlock);
private: void destroying(clientio); };

void
runningjob::run(clientio io) {
    logmsg(loglevel::debug, "start job " + j.field());
    auto _pi(fd_t::pipe());
    if (_pi.isfailure()) {
        result.mkjust(_pi.failure());
        return; }
    auto pi(_pi.success());
    auto p(spawn::process::spawn(
               spawn::program(PREFIX "/runjob" EXESUFFIX)
               .addarg(owner.cp.getconfig().beacon.cluster().field())
               .addarg(owner.fs.name().field())
               .addarg(j.field())
               .addarg("3")
               .addfd(pi.write, 3)));
    if (p.isfailure()) {
        pi.close();
        result.mkjust(p.failure());
        return; }
    pi.write.close();
    buffer b;
    subscriber sub;
    subscription abortsub(sub, shutdown.pub());
    pi.read.nonblock(true).fatal("putting pipe read FD into non-block mode");
    {   iosubscription iosub(sub, pi.read.poll(POLLIN));
        while (true) {
            auto ss(sub.wait(io));
            if (shutdown.ready()) goto killchild;
            if (ss == &iosub) {
                iosub.rearm();
                auto r(b.receivefast(pi.read));
                if (r == error::wouldblock) continue;
                if (r == error::disconnected) break;
                r.fatal("reading from runjob pipe FD"); } } }
    {   spawn::subscription spsub(sub, *p.success());
        while (true) {
            if (shutdown.ready()) goto killchild;
            if (p.success()->hasdied() != Nothing) break;
            sub.wait(io); } }
    pi.read.close();
    b.queue("\0", 1);
    {   auto jr(orerror<jobresult>::parser()
                .match(string((const char *)b.linearise()))
                .flatten()
                .warn("gettin job result from runjob"));
        auto rr(p.success()->join(io));
        logmsg(loglevel::debug,
               "runjob finished with " + jr.field() + " (" + rr.field() + ")");
        if (rr.isright()) jr = error::signalled;
        else if (rr.left() != shutdowncode::ok) jr = error::unknown;
        jr.warn("jobresult result for " + j.field());
        result.mkjust(Steal, jr);
        return; }
    {   killchild:
        p.success()->kill();
        pi.read.close();
        result.mkjust(error::aborted);
        return; } }


void
computeservice::maintenancethread::run(clientio io) {
    subscription ss(sub, owner.shutdown.pub());
    while (!owner.shutdown.ready()) {
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
        auto eid(owner.eqq(muxtoken).queue(
                     proto::compute::event::finish(f),
                     rpcservice2::acquirestxlock(io)));
        logmsg(loglevel::debug,
               "queue finish event " + eid.field() + " for " +
               rj->j.name().field() + " res " + rj->result.just().field());
        owner.runningjobs(muxtoken).drop(rj);
        rj->join(threadtoken.just());
        owner.mux.unlock(&muxtoken); }
    /* Shut it down.  All jobs should be watching the shutdown box, so
     * this should terminate reasonably quickly. */
    logmsg(loglevel::debug, "compute service terminating");
    while (true) {
        auto muxtoken(owner.mux.lock());
        if (owner.runningjobs(muxtoken).empty()) {
            owner.mux.unlock(&muxtoken);
            break; }
        auto j(owner.runningjobs(muxtoken).pophead());
        /* Not really necessary, because we've already shut down the
         * RPC interface, but easy, and makes the API cleaner. */
        auto &f(owner.finishedjobs(muxtoken).append(
                    proto::compute::jobstatus::finished(
                        j->j.name(),
                        j->tag,
                        error::shutdown)));
        auto eid(owner.eqq(muxtoken).queue(
                     proto::compute::event::finish(f),
                     rpcservice2::acquirestxlock(io)));
        logmsg(loglevel::debug,
               "queue shutdown finish event " + eid.field() + " for " +
               j->j.name().field() + " res " + j->result.field());
        owner.mux.unlock(&muxtoken);
        j->subsc = Nothing;
        j->join(io); } }

orerror<nnp<computeservice> >
computeservice::build(clientio io,
                      const clustername &cn,
                      const agentname &fs,
                      const agentname &sn,
                      const filename &statefile) {
    auto cp(connpool::build(cn));
    if (cp.isfailure()) return cp.failure();
    auto &eqs(*eqserver::build());
    auto eqq(eqs.openqueue(proto::eq::names::compute, statefile));
    if (eqq.isfailure()) {
        eqs.destroy();
        cp.success()->destroy();
        return eqq.failure(); }
    return rpcservice2::listen<computeservice>(
        io,
        cn,
        sn,
        peername::all(peername::port::any),
        *cp.success(),
        fs,
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
        logmsg(loglevel::debug, "starting job " + j.field());
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
        logmsg(loglevel::debug, "enumerating jobs");
        auto tok(mux.lock());
        pair<proto::eq::eventid, list<proto::compute::jobstatus> > res(
            eqq(tok).lastid(),
            ::empty);
        for (auto it(runningjobs(tok).start()); !it.finished(); it.next()) {
            res.second().append(
                proto::compute::jobstatus::running((*it)->j.name(),
                                                   (*it)->tag)); }
        for (auto it(finishedjobs(tok).start()); !it.finished(); it.next()) {
            res.second().append(*it); }
        mux.unlock(&tok);
        logmsg(loglevel::debug, "enumerate says " + res.field());
        ic->complete(
            [result = decltype(res)(res, Steal)]
            (serialise1 &s, mutex_t::token /* txlock */, onconnectionthread) {
                s.push(result); },
            acquirestxlock(io),
            oct);
        return Success; }
    else if (t == proto::compute::tag::drop) {
        jobname jn(ds);
        if (ds.isfailure()) return ds.failure();
        logmsg(loglevel::debug, "drop job " + jn.field());
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
            *this,
            j,
            tag,
            shutdown,
            thr.sub));
    auto eid(eqq(tok).queue(proto::compute::event::start(j.name(), tag), atl));
    return mkpair(eid, tag); }

void
computeservice::destroying(clientio io) {
    shutdown.set();
    thr.join(io);
    eqs.destroy();
    _eqq.destroy(io);
    fs.destroy();
    cp.destroy(); } }

orerror<void>
computeagent::format(const filename &f) {
    return eqserver::formatqueue(proto::eq::names::compute, f); }

orerror<nnp<computeagent> >
computeagent::build(clientio io,
                    const clustername &cn,
                    const agentname &fs,
                    const agentname &an,
                    const filename &f) {
    auto r(__computeagent::computeservice::build(io, cn, fs, an, f));
    if (r.isfailure()) return r.failure();
    return _nnp(*(computeagent *)&*r.success()); }

void
computeagent::destroy(clientio io) {
    ((__computeagent::computeservice *)this)->destroy(io); }
