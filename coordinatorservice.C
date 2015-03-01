#include <err.h>

#include "connpool.H"
#include "coordinator.H"
#include "filesystem.H"
#include "job.H"
#include "jobname.H"
#include "logging.H"
#include "rpcservice2.H"
#include "serialise.H"
#include "storage.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "parsers.tmpl"
#include "rpcservice2.tmpl"
#include "thread.tmpl"

class coordinatorservice : public rpcservice2 {
public: filesystem &fs;
public: connpool &pool;

public: class _jobcreator : public thread {
    public: class machine {
            /* The create job state machine. */
            /* XXX once we've picked a storage agent to create the job
             * on, we keep trying to create it there until we're told
             * to stop.  That's possibly sub-optimal if e.g. the agent
             * crashes but some other agent remains available. */
            /* Tags for distinguishing subscriptions more easily. */
        public: static char subscription_ic;
        public: static char subscription_call;
            /* The RPC call which started us going. */
        public: rpcservice2::incompletecall &ic;
            /* Connects the subscriber to ic.abandoned().pub() */
        public: maybe<subscription> icsub;
            /* Event ID at whcih job is created, once it has been
             * created. */
        public: maybe<proto::eq::eventid> eid;
            /* Call to the storage agent. */
        public: connpool::asynccall &call;
            /* Connects the subscriber to call.pub() */
        public: maybe<subscription> callsub;
            /* Where are we creating the job? */
        public: const agentname agent;
            /* What is the job called? */
        public: const jobname jn;
        public: machine(rpcservice2::incompletecall &,
                        const agentname &,
                        const job &,
                        connpool &,
                        subscriber &); };
    public: coordinatorservice &owner;
    public: list<machine> outstanding;
    public: waitbox<void> shutdown;
    public: subscriber sub;
    public: _jobcreator(thread::constoken tok, coordinatorservice &_owner);
    public: void run(clientio); };
public: _jobcreator &jobcreator;

public: coordinatorservice(const constoken &token,
                           connpool &_pool,
                           filesystem &_fs)
    : rpcservice2(token, interfacetype::coordinator),
      fs(_fs),
      pool(_pool),
      jobcreator(
          *thread::start<_jobcreator>(fields::mk("jobcreator"), *this)) {}
public: orerror<void> called(clientio,
                             deserialise1 &,
                             interfacetype,
                             nnp<incompletecall>,
                             onconnectionthread);
public: void destroying(clientio); };

char
coordinatorservice::_jobcreator::machine::subscription_ic;

char
coordinatorservice::_jobcreator::machine::subscription_call;

orerror<void>
coordinatorservice::called(clientio io,
                           deserialise1 &ds,
                           interfacetype t,
                           nnp<incompletecall> ic,
                           onconnectionthread oct) {
    assert(t == interfacetype::coordinator);
    proto::coordinator::tag tag(ds);
    if (tag == proto::coordinator::tag::createjob) {
        job j(ds);
        if (ds.isfailure()) return ds.failure();
        /* Quick fast path to deal with the case where the job already
         * exists somewhere. */
        auto already(fs.findjob(j.name()));
        if (!already.empty()) {
            ic->complete([res = already.peekhead()]
                         (serialise1 &s,
                          mutex_t::token /* txlock */,
                          onconnectionthread) {
                             s.push(res); },
                         acquirestxlock(io),
                         oct);
            return Success; }
        /* Otherwise, start the job creation machine. */
        auto agent(fs.nominateagent());
        if (agent == Nothing) {
            /* No agents available -> fail the call. */
            return error::nostorageagents; }
        /* Otherwise, start the creation machine to complete the call
         * asynchronously. */
        assert(!jobcreator.shutdown.ready());
        jobcreator.outstanding.append(*ic,
                                      agent.just(),
                                      j,
                                      pool,
                                      jobcreator.sub);
        return Success; }
    else {
        /* Tag deserialiser shouldn't let us get here. */
        abort(); } }

void
coordinatorservice::destroying(clientio io) {
    jobcreator.shutdown.set();
    jobcreator.join(io); }

coordinatorservice::_jobcreator::machine::machine(
    rpcservice2::incompletecall &_ic,
    const agentname &_agent,
    const job &j,
    connpool &pool,
    subscriber &sub)
    : ic(_ic),
      icsub(Nothing),
      eid(Nothing),
      call(pool.call(
               _agent,
               interfacetype::storage,
               /* Will be cancelled if our client's call gets
                * cancelled, but not before. */
               Nothing,
               [j] (serialise1 &s, connpool::connlock) {
                   s.push(proto::storage::tag::createjob);
                   s.push(j); },
               [this] (deserialise1 &ds, connpool::connlock) {
                   assert(eid == Nothing);
                   eid.mkjust(ds);
                   return ds.status(); })),
      callsub(Nothing),
      agent(_agent),
      jn(j.name()) {
    icsub.mkjust(sub, ic.abandoned().pub, &subscription_ic);
    callsub.mkjust(sub, call.pub(), &subscription_call); }

coordinatorservice::_jobcreator::_jobcreator(thread::constoken token,
                                             coordinatorservice &_owner)
    : thread(token),
      owner(_owner),
      outstanding(),
      shutdown(),
      sub() {}

void
coordinatorservice::_jobcreator::run(clientio io) {
    subscription ssub(sub, shutdown.pub);
    while (!shutdown.ready()) {
        auto ss(sub.wait(io));
        if (ss == &ssub) continue;
        if (ss->data == &machine::subscription_call) {
            logmsg(loglevel::debug, "woke up for call subscription");
            auto &m(*containerof(ss, machine, callsub.__just()));
            auto t(m.call.finished());
            if (t == Nothing) continue;
            m.callsub = Nothing;
            m.icsub = Nothing;
            auto r(m.call.pop(t.just()));
            if (r.issuccess()) {
                /* Make sure the filesystem cache knows about the new
                 * job before we tell that the client that we're done,
                 * to avoid stupid races. */
                /* Note that this is to some extend best effort: if
                 * the agent drops out of the beacon then we're
                 * definitely going to lose the newly created job.
                 * The point is that callers can assume that if the
                 * job's gone away when we return then something's
                 * gone wrong; there's no need to wait around for a
                 * few milliseconds to see if it's going to
                 * mysteriously reappear. */
                owner.fs.newjob(m.agent, m.jn, m.eid.just());
                m.ic.complete([sn = m.agent]
                              (serialise1 &s, mutex_t::token /* txlock */) {
                                  s.push(sn); },
                              acquirestxlock(io)); }
            else m.ic.fail(r.failure(), acquirestxlock(io));
            for (auto it(outstanding.start()); true; it.next()) {
                if (&*it == &m) {
                    it.remove();
                    break; } } }
        else if (ss->data == &machine::subscription_ic) {
            logmsg(loglevel::debug, "woke up for ic subscription");
            auto &m(*containerof(ss, machine, icsub.__just()));
            if (!m.ic.abandoned().ready()) continue;
            m.callsub = Nothing;
            m.icsub = Nothing;
            m.call.abort();
            m.ic.fail(error::shutdown, acquirestxlock(io));
            for (auto it(outstanding.start()); true; it.next()) {
                if (&*it == &m) {
                    it.remove();
                    break; } } }
        else abort(); }
    /* We're being shut down.  Abandon anything which is still
     * outstanding. */
    while (!outstanding.empty()) {
        auto &i(outstanding.peekhead());
        i.icsub = Nothing;
        i.callsub = Nothing;
        i.ic.fail(error::shutdown, acquirestxlock(io));
        i.call.abort();
        outstanding.pophead(); } }

int
main(int argc, char *argv[]) {
    initlogging("coordinator");
    initpubsub();

    if (argc != 3) {
        errx(1, "need two arguments, the cluster to join and our own name"); }
    auto cluster(parsers::__clustername()
                 .match(argv[1])
                 .fatal("parsing cluster name " + fields::mk(argv[1])));
    auto name(parsers::_agentname()
              .match(argv[2])
              .fatal("parsing agent name " + fields::mk(argv[2])));
    auto bc(beaconclient::build(cluster)
            .fatal("creating beacon client"));
    auto pool(connpool::build(cluster)
              .fatal("creating connection pool"));
    auto &fs(filesystem::build(pool, *bc));

    auto service(rpcservice2::listen<coordinatorservice>(
                     clientio::CLIENTIO,
                     cluster,
                     name,
                     peername::all(peername::port::any),
                     *pool,
                     fs)
                 .fatal("listening on coordinator interface"));

    while (true) timedelta::hours(1).future().sleep(clientio::CLIENTIO);
    return 0; }
