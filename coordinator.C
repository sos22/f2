#include "connpool.H"
#include "either.H"
#include "eq.H"
#include "eqclient.H"
#include "jobname.H"
#include "list.H"
#include "maybe.H"
#include "pubsub.H"
#include "slavename.H"
#include "storage.H"
#include "streamname.H"
#include "streamstatus.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "thread.tmpl"

/* The different types of subscription we use are distinguished by
 * magic pointers in the data field.  Given this you can find the
 * actual structure by doing containerof() on the subscription. */
#define SUBSCRIPTION_LISTSTREAMS ((void *)1ul)
#define SUBSCRIPTION_EQ ((void *)2ul)
#define SUBSCRIPTION_LISTJOBS ((void *)3ul)

class store;

/* A stream held on a remote storage slave. */
class stream {
public: proto::eq::eventid refreshedat;
public: streamstatus status;
public: stream(proto::eq::eventid _refreshedat,
               const streamstatus &_status)
    : refreshedat(_refreshedat),
      status(_status) {} };

/* A job held on a remote storage slave. */
class job {
public: store &sto;
public: proto::eq::eventid refreshedat;
public: jobname name;

    /* A job will have a LISTSTREAMS outstanding if we have any reason
     * to suspect our cached list has diverged from reality in any way
     * which won't be handled by the event queue.  If there's a
     * LISTSTREAMS there'll also be a subscription connecting its
     * completion event to the filesystem thread's subscriber. */
public: connpool::asynccall *liststreams;
public: maybe<subscription> liststreamssub;
    /* Result of the last liststreams. */
public:  maybe<proto::storage::liststreamsres> liststreamsres;

public: list<stream> content;

    /* Removes which had to be deffered because of an outstanding
     * LISTSTREAMS.  Process them when the LISTSTREAMS finishes. */
public: list<pair<proto::eq::eventid, streamname> > deferredremove;

public: job(store &_sto,
            proto::eq::eventid _refreshedat,
            const jobname &_name)
    : sto(_sto),
      refreshedat(_refreshedat),
      name(_name),
      liststreams(NULL),
      liststreamssub(Nothing),
      liststreamsres(Nothing) {}

public: stream *findstream(const streamname &sn);
public: void removestream(stream &);
public: void startliststreams(connpool &pool,
                              subscriber &sub,
                              const maybe<streamname> &cursor);
public: orerror<void> liststreamswoken(connpool &, subscriber &);

public: ~job(); };

/* A remote storage slave. */
class store {
public: slavename name;
    /* At all times we're either connected or trying to connect. */
public: either<nnp<eqclient<proto::storage::event> >,
               nnp<eqclient<proto::storage::event>::asyncconnect> > eventqueue;
    /* Connects the filesystem thread subscriber to the eqclient, if
     * we're ready to take events from it, or the asyncconnect, if
     * we're still trying to connect.  Only ever Nothing transiently
     * while we're switching state. */
public: maybe<subscription> eqsub;

    /* LISTJOBs machine state. */
    /* We may have a LISTJOBS outstanding, if we're trying to recover
     * from having lost our event queue connection. */
public: connpool::asynccall *listjobs;
    /* And a subscription for that. */
public: maybe<subscription> listjobssub;
    /* Result of the last LISTJOBS to complete, until the filesystem
     * thread collects it. */
public: maybe<proto::storage::listjobsres> listjobsres;

public: list<job> jobs;

    /* Jobs which were removed while a LISTJOBS was outstanding.
     * Re-check them all when the LISTJOBS completes. */
public: list<pair<proto::eq::eventid, jobname> > deferredremove;

public: store(
    subscriber &sub,
    eqclient<proto::storage::event>::asyncconnect &_conn,
    const slavename &_name)
        : name(_name),
          eventqueue(right<nnp<eqclient<proto::storage::event> > >(
                         _nnp(_conn))),
          eqsub(Nothing),
          listjobs(NULL),
          listjobssub(Nothing),
          listjobsres(Nothing),
          jobs(),
          deferredremove() {
    eqsub.mkjust(sub, _conn.pub(), SUBSCRIPTION_EQ); }

public: void reconnect(connpool &pool,
                       subscriber &sub);

public: job *findjob(const jobname &jn);
public: void dropjob(job &j);

public: void eqnewjob(proto::eq::eventid eid, const jobname &job);
public: void eqremovejob(proto::eq::eventid eid, const jobname &jn);
public: void eqnewstream(proto::eq::eventid eid,
                         const jobname &jn,
                         const streamname &sn);
public: void eqfinishstream(proto::eq::eventid eid,
                            const jobname &jn,
                            const streamname &sn,
                            const streamstatus &status);
public: void eqremovestream(proto::eq::eventid eid,
                            const jobname &jn,
                            const streamname &sn);
public: void eqevent(connpool &pool, subscriber &sub);

public: orerror<void> eqconnected(connpool &pool, subscriber &sub);
public: orerror<void> eqwoken(connpool &, subscriber &);

public: void startlistjobs(connpool &pool,
                           subscriber &sub,
                           const maybe<jobname> &cursor);
public: orerror<void> listjobswoken(connpool &, subscriber &);

public: ~store(); };

/* Reconnect to the event queue, tearing down any existing partial
 * connections first. */
void
store::reconnect(connpool &pool,
                 subscriber &sub) {
    for (auto j(jobs.start()); !j.finished(); j.next()) {
        if (j->liststreams != NULL) {
            assert(j->liststreamssub != Nothing);
            j->liststreamssub = Nothing;
            /* Don't care about result of this; we're already
             * restarting from the beginning. */
            (void)j->liststreams->abort();
            j->liststreams = NULL; }
        else assert(j->liststreamssub == Nothing);
        j->liststreamsres = Nothing; }
    if (listjobs != NULL) {
        assert(listjobssub != Nothing);
        listjobssub = Nothing;
        (void)listjobs->abort();
        listjobs = NULL; }
    else assert(listjobssub == Nothing);
    listjobsres = Nothing;
    eqsub = Nothing;
    if (eventqueue.isleft()) eventqueue.left()->destroy();
    else eventqueue.right()->abort();
    eventqueue.mkright(eqclient<proto::storage::event>::connect(
                           pool,
                           name,
                           proto::eq::names::storage));
    eqsub.mkjust(sub, eventqueue.right()->pub(), SUBSCRIPTION_EQ); }

void
store::dropjob(job &j) {
    for (auto it(jobs.start()); true; it.next()) {
        if (&j == &*it) {
            it.remove();
            return; } } }

void
job::removestream(stream &s) {
    for (auto it(content.start()); true; it.next()) {
        if (&s == &*it) {
            it.remove();
            return; } } }

job::~job() {
    if (liststreams) {
        liststreamssub = Nothing;
        liststreams->abort(); }
    assert(liststreamssub == Nothing); }

job *
store::findjob(const jobname &jn) {
    for (auto it(jobs.start()); !it.finished(); it.next()) {
        if (it->name == jn) return &*it; }
    return NULL; }

stream *
job::findstream(const streamname &sn) {
    for (auto it(content.start()); !it.finished(); it.next()) {
        if (it->status.name() == sn) return &*it; }
    return NULL; }

void
job::startliststreams(connpool &pool,
                      subscriber &sub,
                      const maybe<streamname> &cursor) {
    assert(liststreams == NULL);
    assert(liststreamssub == Nothing);
    liststreams = pool.call(
        sto.name,
        interfacetype::storage,
        /* Will be cancelled if the store drops from the beacon or the
         * job drops from the store. */
        Nothing,
        [this, cursor] (serialise1 &s, connpool::connlock) {
            s.push(proto::storage::tag::liststreams);
            s.push(name);
            s.push(cursor);
            s.push(maybe<unsigned>(Nothing)); },
        [this] (deserialise1 &ds, connpool::connlock) {
            assert(liststreamsres == Nothing);
            liststreamsres.mkjust(ds);
            return ds.status(); });
    liststreamssub.mkjust(sub, liststreams->pub(), SUBSCRIPTION_LISTSTREAMS); }

void
store::eqnewjob(proto::eq::eventid eid, const jobname &job) {
    /* Check whether we've already got the job in the list. */
    auto j(findjob(job));
    if (j == NULL) {
        /* New job. */
        jobs.append(*this, eid, job);
        /* No LISTSTREAMS: if we saw the newjob event, we will also
         * see all the newstream ones. */ }
    else {
        /* Job already exists.  Mostly a no-op, except that once we've
         * gotten the t_newjob event we're guaranteed to get
         * t_newstream events for any streams in the job, so we can
         * cancel any outstanding LISTSTREAMS. */
        if (j->liststreams != NULL) {
            assert(j->liststreamssub != Nothing);
            j->liststreamssub = Nothing;
            j->liststreams->abort();
            j->liststreams = NULL;
            j->liststreamsres = Nothing; } } }

void
store::eqremovejob(proto::eq::eventid eid, const jobname &job) {
    auto j(findjob(job));
    if (j != NULL) {
        /* If our cache is more up to date than this event then it
         * must be a stale event. */
        if (j->refreshedat > eid) {
            logmsg(loglevel::info,
                   "drop stale removejob event " + fields::mk(job) +
                   " at " + fields::mk(eid) +
                   "; cache at " + fields::mk(j->refreshedat));
            return; }
        /* Otherwise, the job is dead and we need to remove it from
         * the cache. */
        dropjob(*j);
        /* Fall through to the reorder-with-LISTJOBS handling, because
         * that's generally a lot easier to understand. */ }
    else {
        logmsg(loglevel::info,
               "cannot find job to remove: " + fields::mk(job)); }
    if (listjobs != NULL) {
        /* Watch out for event reordering between removejob and
         * LISTJOBs.  We might get something like this:
         *
         * 1) Start a LISTJOBS.
         * 2) LISTJOBS sees job X.
         * 3) Job X removed.
         * 4) We receive the removejob event for X
         * 5) LISTJOBS completes.
         *
         * We need to make sure that we don't accidentally resurrect
         * X.  Handle it by storing removes in a queue which gets
         * processed when LISTJOBS completes. */
        logmsg(loglevel::info,
               "defer remove " + fields::mk(job) +
               " because LISTJOBS outstanding");
        deferredremove.append(mkpair(eid, job)); } }

void
store::eqnewstream(proto::eq::eventid eid,
                   const jobname &jn,
                   const streamname &sn) {
    auto j(findjob(jn));
    if (j == NULL) {
        /* Drop newstream events for jobs we don't know about.  If the
         * job is still live then we'll eventually find out about it
         * and recover that way, and if it's been removed then we
         * don't need to do anything at all. */
        logmsg(loglevel::info,
               "drop new stream event " + fields::mk(jn) +
               "::" + fields::mk(sn) +
               " at " + fields::mk(eid));
        return; }
    auto s(j->findstream(sn));
    if (s != NULL) {
        /* We already know about this stream, so the newstream event
         * must have been delayed. */
        if (eid > s->refreshedat) {
            /* This shouldn't happen.  We thought the stream was live
             * at time T and we just got an event saying it was
             * created at time T+n.  That implies that either we were
             * wrong to say it was live at T or that we've missed a
             * removestream event between T and T+n. */
            logmsg(loglevel::error,
                   "detected inconsistency in stream liveness"
                   " from new stream event " + fields::mk(jn) +
                   "::" + fields::mk(sn) +
                   " at " + fields::mk(eid) +
                   " on " + fields::mk(name) +
                   "; was " + fields::mk(s->status) +
                   " at " + fields::mk(s->refreshedat));
            /* Update the cached liveness and hope for the best. */
            s->refreshedat = eid; }
        else {
            /* We already knew it was live at time T and we were just
             * told it was created at time T-n.  That's fine. */
            logmsg(loglevel::info,
                   "drop old new stream event " + fields::mk(jn) +
                   "::" + fields::mk(sn) +
                   " at " + fields::mk(eid) +
                   "; cache at " + s->refreshedat.field()); } }
    else {
        /* Fresh stream.  Add it to the job. */
        j->content.append(eid, streamstatus::empty(sn)); } }

void
store::eqfinishstream(proto::eq::eventid eid,
                      const jobname &jn,
                      const streamname &sn,
                      const streamstatus &status) {
    auto j(findjob(jn));
    if (j == NULL) {
        /* We don't know about this job.  Either it's been removed, in
         * which case we don't care about the event, or our LISTJOBS
         * is still outstanding, in which case we'll recover when that
         * finishes. */
        logmsg(loglevel::info,
               "drop finish stream event " + fields::mk(jn) +
               "::" + fields::mk(sn) +
               " -> " + fields::mk(status) +
               " at " + fields::mk(eid) +
               "; no job");
        return; }
    auto s(j->findstream(sn));
    if (s == NULL) {
        /* Similarly don't care about finishing streams we don't know
         * about. */
        logmsg(loglevel::info,
               "drop finish stream event " + fields::mk(jn) +
               "::" + fields::mk(sn) +
               " -> " + fields::mk(status) +
               " at " + fields::mk(eid) +
               "; no stream");
        return; }
    if (s->refreshedat >= eid) {
        /* This is an obsolete event.  Drop it. */
        logmsg(loglevel::info,
               "drop finish stream event " + fields::mk(jn) +
               "::" + fields::mk(sn) +
               " -> " + fields::mk(status) +
               " at " + fields::mk(eid) +
               "; cache " + fields::mk(s->status) +
               " at " + fields::mk(s->refreshedat));
        return; }
    if (s->status.isfinished()) {
        /* We knew the stream was finished at or before T, and we're
         * now being told it only get finished at T+n.  That implies
         * we've had a rewind. */
        logmsg(loglevel::error,
               "detected inconsistency in stream lifecycle"
               " from finish stream event " + fields::mk(jn) +
               "::" + fields::mk(sn) +
               " -> " + fields::mk(status) +
               " at " + fields::mk(eid) +
               "; cache " + fields::mk(s->status) +
               " at " + fields::mk(s->refreshedat));
        /* apply it anyway. */ }
    /* This is the finish of this stream. */
    s->status = status;
    s->refreshedat = eid; }

void
store::eqremovestream(proto::eq::eventid eid,
                      const jobname &jn,
                      const streamname &sn) {
    auto j(findjob(jn));
    if (j == NULL) {
        /* If we don't have the job then we don't have any of its
         * streams and we don't have any LISTSTREAMS for it, so we're
         * fine. */
        return; }
    auto s(j->findstream(sn));
    if (s) {
        if (s->refreshedat >= eid) {
            /* Already know stream alive at time T, so can safely
             * ignore message that it was removed at T-n, because it
             * was reconstructed and the remove message got delayed
             * after a LISTSTREAMS. */
            logmsg(loglevel::debug,
                   "drop remove stream " + fields::mk(jn) +
                   "::" + fields::mk(sn) +
                   " at " + fields::mk(eid) +
                   " because stream live at " +
                   fields::mk(s->refreshedat));
            return; }
        /* This is the actual remove operation. */
        j->removestream(*s); }
    /* Need to make sure the remove isn't reordered with the discovery
     * of the stream in a LISTSTREAMS, to avoid resurrection bugs. */
    if (j->liststreams != NULL) {
        j->deferredremove.append(mkpair(eid, sn));
        logmsg(loglevel::debug,
               "defer remove " + fields::mk(jn) +
               "::" + fields::mk(sn) +
               " at " + fields::mk(eid) +
               " because of outstanding LISTSTREAMS"); }
    else if (s == NULL) {
        logmsg(loglevel::debug,
               "drop remove stream " + fields::mk(jn) +
               "::" + fields::mk(sn) +
               " at " + fields::mk(eid) +
               " because it's already gone"); }
    else {
        logmsg(loglevel::debug,
               "process remove stream " + fields::mk(jn) +
               "::" + fields::mk(sn) +
               " at " + fields::mk(eid) +
               " to completion"); } }

/* Check for events on the queue.  Note that this responds to an error
 * by reconnecting the event queue.  It's only a proper error if that
 * reconnection fails. */
void
store::eqevent(connpool &pool, subscriber &sub) {
    auto ev(eventqueue.left()->popid());
    if (ev == Nothing) return;
    if (ev.just().isfailure()) {
        /* Error on the event queue.  Tear it down, abort all
         * outstanding calls, and restart the machinery from the
         * beginning. */
        logmsg(loglevel::failure,
               "lost eq to " + fields::mk(name) +
               ": " + fields::mk(ev.just().failure()));
        reconnect(pool, sub);
        return; }
    auto eid(ev.just().success().first());
    auto &evt(ev.just().success().second());
    switch (evt.typ) {
    case proto::storage::event::t_newjob:
        return eqnewjob(eid, evt.job);
    case proto::storage::event::t_removejob:
        return eqremovejob(eid, evt.job);
    case proto::storage::event::t_newstream:
        return eqnewstream(eid, evt.job, evt.stream.just());
    case proto::storage::event::t_finishstream:
        return eqfinishstream(eid,
                              evt.job,
                              evt.stream.just(),
                              evt.status.just());
    case proto::storage::event::t_removestream:
        return eqremovestream(eid, evt.job, evt.stream.just()); }
    abort(); }

orerror<void>
job::liststreamswoken(connpool &pool, subscriber &sub) {
    assert(liststreamssub != Nothing);
    {   auto t(liststreams->finished());
        if (t == Nothing) return Success;
        liststreamssub = Nothing;
        auto r(liststreams->pop(t.just()));
        if (r.isfailure()) {
            logmsg(loglevel::failure,
                   "LISTSTREAMS on " + fields::mk(sto.name) +
                   "::" + fields::mk(name) + ": " +
                   fields::mk(r.failure()));
            return r.failure(); } }
    assert(liststreamsres != Nothing);
    auto &result(liststreamsres.just());
    /* Remove any streams which have died. */
    bool lost;
    for (auto it(content.start());
         !it.finished();
         lost ? it.remove() : it.next()) {
        lost = false;
        /* Ignore streams which aren't in the results range. */
        if (result.start == Nothing ||
            it->status.name() < result.start.just()) continue;
        if (result.end == Nothing ||
            it->status.name() >= result.end.just()) continue;
        /* Ignore streams where our existing knowledge is more up to date
         * than the new result. */
        if (it->refreshedat >= result.when) continue;
        /* If the stream isn't in the result set then it's been lost. */
        lost = true;
        for (auto it2(result.res.start()); lost && !it2.finished(); it2.next()){
            lost = it->status.name() != it2->name(); }
        if (lost) {
            /* The common case is that lost streams are detected by
             * event queue messages, so this should be pretty rare,
             * but it can sometimes happen if we lose our EQ
             * subscription and have to recover. */
            logmsg(loglevel::info,
                   "lost stream " + fields::mk(name) +
                   "::" + fields::mk(it->status.name()) +
                   " on " + fields::mk(sto.name) +
                   " to LISTSTREAMS result " + fields::mk(result)); } }
    /* Integrate the result streams into our cache. */
    for (auto it(result.res.start()); !it.finished(); it.next()) {
        auto s(findstream(it->name()));
        if (s != NULL) {
            /* Already know about this stream.  If what we know is
             * more up to date than the new data we can ignore it. */
            if (s->refreshedat < result.when) {
                logmsg(loglevel::debug,
                       "refreshed stream " + fields::mk(sto.name) +
                       "::" + fields::mk(name) +
                       " -> " + fields::mk(s->status) +
                       " at " + fields::mk(s->refreshedat) +
                       " to " + fields::mk(*it) +
                       " at " + fields::mk(result.when));
                s->status = *it;
                s->refreshedat = result.when; } }
        else {
            /* New stream -> create it. */
            logmsg(loglevel::debug,
                   "discovered stream " + fields::mk(*it) + " in " +
                   fields::mk(name) + " on " + fields::mk(sto.name) +
                   " at " + fields::mk(result.when));
            content.append(result.when, *it); } }
    /* Flush the deferred events queue. */
    while (!deferredremove.empty()) {
        auto r(deferredremove.pophead());
        /* XXX should maybe move eqremovestream from class store to
         * class job, for general cleanliness? */
        sto.eqremovestream(r.first(), name, r.second()); }
    /* If the server truncated the results then kick off another call
     * to get the rest. */
    auto next(result.end);
    liststreamsres = Nothing;
    if (next != Nothing) startliststreams(pool, sub, next);
    return Success; }

/* Check if a store's finished connecting to its event queue.  If the
 * EQ connect fails then we'll drop the store. */
orerror<void>
store::eqconnected(connpool &pool, subscriber &sub) {
    /* Shouldn't have LISTJOBS or LISTSTREAMS calls outstanding if we
     * don't have a queue. */
    assert(listjobs == NULL);
    for (auto it(jobs.start()); !it.finished(); it.next()) {
        assert(it->liststreams == NULL); }
    auto t(eventqueue.right()->finished());
    if (t == Nothing) return Success;
    eqsub = Nothing;
    auto res(eventqueue.right()->pop(t.just()));
    if (res.isfailure()) {
        logmsg(loglevel::failure,
               "cannot connect to " + fields::mk(name) +
               " event queue: " + fields::mk(res.failure()));
        /* Give up on this one for now. */
        /* XXX we'll retry when the beacon next wakes us up, which is
         * unlikely to be the optimal strategy for this kind of
         * failure. */
        return res.failure(); }
    eventqueue.mkleft(res.success());
    eqsub.mkjust(sub, eventqueue.left()->pub(), SUBSCRIPTION_EQ);
    
    /* We may have missed some newjob events.  Start a LISTJOBS
     * machine to regenerate them. */
    startlistjobs(pool, sub, Nothing);
    
    /* If we have any jobs then we may also have missed some stream
     * events.  Start LISTSTREAMS machines for all of the jobs to
     * catch up again. */
    for (auto j(jobs.start()); !j.finished(); j.next()) {
        j->startliststreams(pool, sub, Nothing); }
    return Success; }

/* Event queue changed, either by completing the connect or by
 * receiving another event. */
orerror<void>
store::eqwoken(connpool &pool, subscriber &sub) {
    if (eventqueue.isright()) return eqconnected(pool, sub);
    else {
        eqevent(pool, sub);
        return Success; } }

/* Start a LISTJOBS call.  Start from the beginning if the cursor is
 * Nothing, otherwise restart from where we left off. */
void
store::startlistjobs(connpool &pool,
                     subscriber &sub,
                     const maybe<jobname> &cursor) {
    assert(listjobs == NULL);
    assert(listjobssub == Nothing);
    listjobs = pool.call(
        name,
        interfacetype::storage,
        /* Infinite timeout is safe: we'll give up when it drops out
         * of the beacon client. */
        Nothing,
        [cursor] (serialise1 &s, connpool::connlock) {
            s.push(proto::storage::tag::listjobs);
            s.push(cursor);
            s.push(maybe<unsigned>(Nothing)); },
        [this] (deserialise1 &ds, connpool::connlock) {
            assert(listjobsres == Nothing);
            /* No sync: the filesystem thread won't look at our result
             * until the RPC call completes.  Just deserialise the
             * result straight out. */
            listjobsres.mkjust(ds);
            return ds.status(); });
    listjobssub.mkjust(sub, listjobs->pub(), SUBSCRIPTION_LISTJOBS); }

/* Called whenever something might have happened to a store LISTJOBS
 * call.  Responsible for checking whether it's finished, and, if so,
 * processing the results and potentially starting another call.  If
 * this returns an error then we'll drop the store from the list. */
orerror<void>
store::listjobswoken(connpool &pool, subscriber &sub) {
    assert(listjobssub != Nothing);
    assert(listjobs != NULL);
    {   auto t(listjobs->finished());
        if (t == Nothing) return Success;
        listjobssub = Nothing;
        auto r(listjobs->pop(t.just()));
        listjobs = NULL;
        if (r.isfailure()) {
            logmsg(loglevel::failure,
                   "LISTJOBS on " + name.field() +
                   ": " + r.failure().field());
            return r.failure(); } }
    /* No sync: the LISTJOBS call has finished. */
    /* listjobsres must be filled out when a LISTJOBS succeeds. */
    assert(listjobsres != Nothing);
    auto &result(listjobsres.just());
    /* Remove any jobs which have died. */
    bool found;
    for (auto it(jobs.start());
         !it.finished();
         found ? it.next() : it.remove()) {
        found = true;
        /* Ignore jobs which aren't in the results range. */
        if (result.start == Nothing || it->name < result.start.just()) continue;
        if (result.end == Nothing || it->name >= result.end.just()) continue;
        /* Ignore jobs where our existing knowledge is more up to date
         * than the new result. */
        if (it->refreshedat >= result.when) continue;
        /* If the job isn't in the result set then it's been lost. */
        found = false;
        for (auto it2(result.res.start()); !found&&!it2.finished(); it2.next()){
            found = it->name == *it2; }
        if (!found) {
            /* The common case is that lost jobs are detected by event
             * queue messages, so this should be pretty rare, but it
             * can sometimes happen if we lose our EQ subscription and
             * have to recover. */
            logmsg(loglevel::info,
                   "lost job " + fields::mk(it->name) + " on " +
                   fields::mk(name) + " to LISTJOBS result " +
                   fields::mk(result)); } }
    /* Integrate any new jobs into the list. */
    for (auto it(result.res.start()); !it.finished(); it.next()) {
        auto j(findjob(*it));
        if (j != NULL) {
            /* Already have it, so just update the liveness
             * timestamp. */
            if (j->refreshedat < result.when) j->refreshedat = result.when; }
        else {
            /* New job -> create it and start looking for streams in
             * it. */
            logmsg(loglevel::debug,
                   "discovered job " + fields::mk(*it) + " on " +
                   fields::mk(name));
            jobs.append(*this, result.when, *it)
                .startliststreams(pool,
                                  sub,
                                  Nothing); } }
    /* Flush the deferred events queue. */
    while (!deferredremove.empty()) {
        auto r(deferredremove.pophead());
        eqremovejob(r.first(), r.second()); }
    /* If the server truncated the results then kick off another call
     * to get the rest. */
    auto next(result.end);
    listjobsres = Nothing;
    if (next != Nothing) startlistjobs(pool, sub, next);
    return Success; }

/* Store destructor.  This can be called when the store is any state,
 * so it must do things like cancelling outstanding async calls. */
store::~store() {
    eqsub = Nothing;
    if (eventqueue.isleft()) eventqueue.left()->destroy();
    else eventqueue.right()->abort();
    listjobssub = Nothing;
    if (listjobs != NULL) listjobs->abort(); }

/* The filesystem is a cache recording the content of each storage
 * slave known to the beacon.  It is updated asynchronously, and can
 * therefore lag behind reality, and can even occasionally reorder
 * events.  It does, however, give users a couple of useful guarantees:
 *
 * -- It preserves monotonicity for individual objects (jobs and
 *    streams), in the sense that if an object is seen going from
 *    state X to state Y in the filesystem then it must have gone from
 *    X to Y in reality.  It may sometimes combine edges, so if the
 *    real object transitions X to Y to Z then the filesystem object
 *    might go directly from X to Z, but that should be
 *    indistinguishable from the user just not getting scheduled in
 *    the right place.
 * -- The system is eventually consistent, so any change in the real
 *    world will eventually be reflected in the cache, and if the real
 *    world stops changing for long enough the cache will eventually
 *    converge on the truth.  There is no hard deadline for
 *    convergence, but under normal load it should be at most a few
 *    seconds, and will often be quicker.
 *
 * Note, though, that the cache does *not* preserve ordering of state
 * changes on different objects.  So, for instance, if in the real
 * world someone creates stream X and then stream Y the cache might
 * notice the creation of Y before the creation of X.
 */
class filesystem : public thread {
private: connpool &pool;
private: beaconclient &bc;
private: waitbox<void> shutdown;

    /* All of the stores which the beacon's told us about. */
public:  list<store> stores;

public:  explicit filesystem(const thread::constoken &token,
                             connpool &_pool,
                             beaconclient &_bc)
    : thread(token),
      pool(_pool),
      bc(_bc),
      shutdown(),
      stores() {}

public:  static filesystem &build(connpool &, beaconclient &);
public:  void run(clientio);

public:  void beaconwoken(subscriber &sub);

public:  void dropstore(store &); };

/* Construct a new filesystem which will keep track of all stores
 * known to the beacon client. */
filesystem &
filesystem::build(connpool &_pool, beaconclient &_bc) {
    return *thread::start<filesystem>(fields::mk("filesystem"), _pool, _bc); }

void
filesystem::run(clientio io) {
    subscriber sub;
    subscription bcsub(sub, bc.changed());
    subscription sssub(sub, shutdown.pub);
    /* Force an initial beacon scan. */
    bcsub.set();
    while (!shutdown.ready()) {
        auto s(sub.wait(io));
        if (s == &sssub) continue;
        else if (s == &bcsub) beaconwoken(sub);
        else if (s->data == SUBSCRIPTION_EQ) {
            auto sto(containerof(static_cast<subscription *>(s),
                                 store,
                                 eqsub.__just()));
            auto res(sto->eqwoken(pool, sub));
            if (res.isfailure()) dropstore(*sto); }
        else if (s->data == SUBSCRIPTION_LISTJOBS) {
            auto sto(containerof(static_cast<subscription *>(s),
                                 store,
                                 listjobssub.__just()));
            auto res(sto->listjobswoken(pool, sub));
            if (res.isfailure()) dropstore(*sto); }
        else if (s->data == SUBSCRIPTION_LISTSTREAMS) {
            auto j(containerof(static_cast<subscription *>(s),
                               job,
                               liststreamssub.__just()));
            auto res(j->liststreamswoken(pool, sub));
            if (res.isfailure()) j->sto.dropjob(*j); }
        else abort(); }
    /* The stores might have references to our subscriber block, so
     * they must be flushed before we return. */
    stores.flush(); }

/* Helper function which gets called whenever there's any possibility
 * that the beacon content has changed.  Compare the beacon results to
 * the cache and update the cache as appropriate. */
void
filesystem::beaconwoken(subscriber &sub) {
    logmsg(loglevel::debug, "scan for new storage slaves");
    list<list<store>::iter> lost;
    for (auto it(stores.start()); !it.finished(); it.next()) lost.pushtail(it);
    for (auto it(bc.start(interfacetype::storage)); !it.finished(); it.next()) {
        bool found = false;
        for (auto it2(lost.start()); !it2.finished(); it2.next()) {
            if (it.name() == (*it2)->name) {
                logmsg(loglevel::verbose, fields::mk(it.name()) + " unchanged");
                found = true;
                it2.remove();
                break; } }
        if (!found) {
            logmsg(loglevel::info,
                   "new storage slave " + fields::mk(it.name()));
            stores.append(sub,
                          eqclient<proto::storage::event>::connect(
                              pool,
                              it.name(),
                              proto::eq::names::storage),
                          it.name()); } }
    for (auto it(lost.start()); !it.finished(); it.next()) {
        logmsg(loglevel::info,
               "lost storage slave " + fields::mk((*it)->name));
        it->remove(); } }


/* Remove a store from the stores list and release it out.  Only used
 * from error paths, so the O(n) cost doesn't really matter. */
void
filesystem::dropstore(store &s) {
    for (auto it(stores.start()); true; it.next()) {
        if (&s == &*it) {
            it.remove();
            return; } } }

#include "err.h"
#include "parsers.H"

int
main(int argc, char *argv[]) {
    initlogging("coordinator");
    initpubsub();

    if (argc != 2) errx(1, "need one argument, the cluster to join.");
    auto cluster(parsers::__clustername()
                 .match(argv[1])
                 .fatal("parsing cluser name " + fields::mk(argv[1])));
    auto bc(beaconclient::build(cluster)
            .fatal("creating beacon client"));
    auto pool(connpool::build(cluster)
              .fatal("creating connection pool"));
    auto &fs(filesystem::build(pool, *bc));

    while (true) timedelta::hours(1).future().sleep(clientio::CLIENTIO);
    return 0; }
