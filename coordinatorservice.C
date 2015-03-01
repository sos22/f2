#include <err.h>

#include "connpool.H"
#include "coordinator.H"
#include "filesystemproto.H"
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
public: agentname &fs;
public: connpool &pool;

public: class _jobcreator : public thread {
    public: class machine {
            /* The create job state machine.  We start by issuing a
             * NOMINATEAGENT call against the filesystem, and then
             * once we get the results of that back we move to issuing
             * CREATEJOB against the storage agent, and then do a
             * STORAGEBARRIER against the filesystem, and then return
             * to our own client. */
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
            /* Event ID at which job is created, once it has been
             * created. */
        public: maybe<proto::eq::eventid> eid;
            /* Call to the filesystem or storage agent. */
        public: connpool::asynccall *call;
            /* Connects the subscriber to call.pub() */
        public: maybe<subscription> callsub;
            /* Where are we creating the job, once we know (or
             * mkjust(Nothing) if the filesystem says to give up). */
        public: maybe<maybe<agentname> > agent;
            /* Set once we've finished synchronising with the
             * filesystem. */
        public: bool synchronised;
            /* Job we're trying to create. */
        public: const job j;
        public: machine(rpcservice2::incompletecall &,
                        const agentname &_fs,
                        const job &,
                        connpool &,
                        subscriber &); };
    public: coordinatorservice &owner;
    public: list<machine> outstanding;
    public: waitbox<void> shutdown;
    public: subscriber sub;
    public: _jobcreator(thread::constoken tok, coordinatorservice &_owner);
    public: void run(clientio);
    public: void fail(machine &m, error why, acquirestxlock);
    public: void callevt(machine &m, acquirestxlock); };
public: _jobcreator &jobcreator;

public: coordinatorservice(const constoken &token,
                           agentname &_fs,
                           connpool &_pool)
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
coordinatorservice::called(clientio,
                           deserialise1 &ds,
                           interfacetype t,
                           nnp<incompletecall> ic,
                           onconnectionthread) {
    assert(t == interfacetype::coordinator);
    proto::coordinator::tag tag(ds);
    if (tag == proto::coordinator::tag::createjob) {
        job j(ds);
        if (ds.isfailure()) return ds.failure();
        assert(!jobcreator.shutdown.ready());
        jobcreator.outstanding.append(*ic,
                                      fs,
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
    const agentname &_fs,
    const job &_j,
    connpool &pool,
    subscriber &sub)
    : ic(_ic),
      icsub(Just(), sub, ic.abandoned().pub, &subscription_ic),
      eid(Nothing),
      call(pool.call(
               _fs,
               interfacetype::filesystem,
               /* Will be cancelled if our client's call gets
                * cancelled, but not before. */
               Nothing,
               [jn = _j.name()] (serialise1 &s, connpool::connlock) {
                   s.push(proto::filesystem::tag::nominateagent);
                   s.push(mkjust(jn)); },
               [this] (deserialise1 &ds, connpool::connlock) {
                   assert(agent == Nothing);
                   agent.mkjust(ds);
                   return ds.status(); })),
      callsub(Just(), sub, call->pub(), &subscription_call),
      agent(Nothing),
      j(_j) { }

coordinatorservice::_jobcreator::_jobcreator(thread::constoken token,
                                             coordinatorservice &_owner)
    : thread(token),
      owner(_owner),
      outstanding(),
      shutdown(),
      sub() {}

void
coordinatorservice::_jobcreator::fail(machine &m,
                                      error why,
                                      acquirestxlock atl) {
    m.callsub = Nothing;
    if (m.call != NULL) m.call->abort();
    m.icsub = Nothing;
    m.ic.fail(why, atl);
    outstanding.drop(&m); }

void
coordinatorservice::_jobcreator::callevt(machine &m, acquirestxlock atl) {
    auto t(m.call->finished());
    if (t == Nothing) return;
    m.callsub = Nothing;
    auto r(m.call->pop(t.just()));
    m.call = NULL;
    if (r.isfailure()) {
        /* Downstream call failed -> upstream call fails as well. */
        fail(m, r.failure(), atl); }
    else if (m.agent == Nothing) {
        /* The agent should have been set by the first successful call. */
        abort(); }
    else if (m.agent.just() == Nothing) {
        /* Filesystem said we have no storage agents.  Fail out. */
        fail(m, error::nostorageagents, atl); }
    else if (m.eid == Nothing) {
        /* Have an agent but not an EID -> time to acquire the EID
         * from the agent. */
        m.call = owner.pool.call(
            m.agent.just().just(),
            interfacetype::storage,
            Nothing,
            [&m] (serialise1 &s, connpool::connlock) {
                s.push(proto::storage::tag::createjob);
                s.push(m.j); },
            [&m] (deserialise1 &ds, connpool::connlock) {
                assert(m.eid == Nothing);
                m.eid.mkjust(ds);
                return ds.status(); });
        m.callsub.mkjust(sub, m.call->pub(), &machine::subscription_call); }
    else if (!m.synchronised) {
        /* We've got a storage agent and an EID -> need to synchronise
         * with the filesystem. */
        m.call = owner.pool.call(
            owner.fs,
            interfacetype::filesystem,
            Nothing,
            [&m] (serialise1 &s, connpool::connlock) {
                s.push(proto::filesystem::tag::storagebarrier);
                s.push(m.agent.just().just());
                s.push(m.eid.just()); },
            [&m] (deserialise1 &ds, connpool::connlock) {
                m.synchronised = true;
                return ds.status(); });
        m.callsub.mkjust(sub, m.call->pub(), &machine::subscription_call); }
    else {
        /* Success! */
        m.icsub = Nothing;
        m.ic.complete(
            [sn = m.agent.just().just()]
            (serialise1 &s, mutex_t::token /* txlock */) {
                s.push(sn); },
            atl);
        outstanding.drop(&m); } }

void
coordinatorservice::_jobcreator::run(clientio io) {
    subscription ssub(sub, shutdown.pub);
    acquirestxlock atl(io);
    while (!shutdown.ready()) {
        auto ss(sub.wait(io));
        if (ss == &ssub) continue;
        else if (ss->data == &machine::subscription_call) {
            logmsg(loglevel::debug, "woke up for call subscription");
            callevt(*containerof(ss, machine, callsub.__just()), atl); }
        else if (ss->data == &machine::subscription_ic) {
            logmsg(loglevel::debug, "woke up for ic subscription");
            auto &m(*containerof(ss, machine, icsub.__just()));
            if (!m.ic.abandoned().ready()) continue;
            fail(m, error::shutdown, atl); }
        else abort(); }
    /* We're being shut down.  Abandon anything which is still
     * outstanding. */
    while (!outstanding.empty()) {
        fail(outstanding.peekhead(), error::shutdown, atl); } }

int
main(int argc, char *argv[]) {
    initlogging("coordinator");
    initpubsub();

    if (argc != 4) {
        errx(1,
             "need three arguments: the cluster to join, the filesystem "
             "name, and our own name"); }
    auto cluster(parsers::__clustername()
                 .match(argv[1])
                 .fatal("parsing cluster name " + fields::mk(argv[1])));
    auto name(parsers::_agentname()
              .match(argv[2])
              .fatal("parsing agent name " + fields::mk(argv[2])));
    auto fs(parsers::_agentname()
            .match(argv[3])
            .fatal("parsing agent name " + fields::mk(argv[3])));
    auto bc(beaconclient::build(cluster)
            .fatal("creating beacon client"));
    auto pool(connpool::build(cluster)
              .fatal("creating connection pool"));

    auto service(rpcservice2::listen<coordinatorservice>(
                     clientio::CLIENTIO,
                     cluster,
                     name,
                     peername::all(peername::port::any),
                     fs,
                     *pool)
                 .fatal("listening on coordinator interface"));

    while (true) timedelta::hours(1).future().sleep(clientio::CLIENTIO);
    return 0; }
