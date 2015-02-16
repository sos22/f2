#include "filesystem.H"

#include "eqclient.H"
#include "jobname.H"
#include "storage.H"
#include "streamstatus.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "thread.tmpl"

namespace filesystemimpl {

/* The different types of subscription we use are distinguished by
 * magic pointers in the data field.  Given this you can find the
 * actual structure by doing containerof() on the subscription. */
#define SUBSCRIPTION_LISTSTREAMS ((void *)1ul)
#define SUBSCRIPTION_EQ ((void *)2ul)
#define SUBSCRIPTION_LISTJOBS ((void *)3ul)

class store;

class onfilesystemthread {};

/* A stream held on a remote storage slave. */
class stream {
public: proto::eq::eventid _refreshedat;
public: proto::eq::eventid &refreshedat(onfilesystemthread) {
    return _refreshedat; }
    
public: streamstatus _status;
public: streamstatus &status(mutex_t::token, onfilesystemthread) {
    return _status; }
public: const streamstatus &status(mutex_t::token) const {
    return _status; }
public: const streamstatus &status(onfilesystemthread) const {
    return _status; }
    
public: stream(proto::eq::eventid __refreshedat,
               const streamstatus &__status)
    : _refreshedat(__refreshedat),
      _status(__status) {} };

/* A job held on a remote storage slave. */
class job {
    /* Tag which is valid if either you're in the LISTSTREAMS call or
     * you've ensured that LISTSTREAMS isn't running. */
public: class inliststreamscall {
    private: inliststreamscall() {}
    public:  inliststreamscall(connpool::connlock) {}
    public:  static inliststreamscall mk(mutex_t::token) {
        return inliststreamscall(); } };
public: store &sto;
public: const jobname name;

public: proto::eq::eventid _refreshedat;
public: proto::eq::eventid refreshedat(onfilesystemthread) const {
    return _refreshedat; }
public: proto::eq::eventid &refreshedat(onfilesystemthread) {
    return _refreshedat; }

    /* A job will have a LISTSTREAMS outstanding if we have any reason
     * to suspect our cached list has diverged from reality in any way
     * which won't be handled by the event queue.  If there's a
     * LISTSTREAMS there'll also be a subscription connecting its
     * completion event to the filesystem thread's subscriber. */
public: connpool::asynccall *_liststreams;
public: connpool::asynccall *&liststreams(mutex_t::token) {
    return _liststreams; }
public: connpool::asynccall *liststreams(mutex_t::token) const {
    return _liststreams; }
public: maybe<subscription> _liststreamssub;
public: maybe<subscription> &liststreamssub(mutex_t::token) {
    return _liststreamssub; }
    /* Result of the last liststreams. */
public:  maybe<proto::storage::liststreamsres> _liststreamsres;
public:  maybe<proto::storage::liststreamsres> &liststreamsres(
    inliststreamscall) { return _liststreamsres; }

public: orerror<maybe<inliststreamscall> > endliststreamscall(
    mutex_t::token);
public: inliststreamscall abortliststreamscall(mutex_t::token);

public: list<stream> _content;
public: list<stream> &content(mutex_t::token, onfilesystemthread) {
    return _content; }
public: const list<stream> &content(mutex_t::token) const {
    return _content; }
public: const list<stream> &content(onfilesystemthread) const {
    return _content; }
    /* This allows you to modify entries in the list, but not to
     * modify the structure of the list itself i.e. no adding or
     * removing entries. */
public: list<stream> &content_unsafe(onfilesystemthread) {
    return _content; }

    /* Removes which had to be deffered because of an outstanding
     * LISTSTREAMS.  Process them when the LISTSTREAMS finishes. */
public: list<pair<proto::eq::eventid, streamname> > _deferredremove;
public: list<pair<proto::eq::eventid, streamname> > &deferredremove(
    onfilesystemthread) { return _deferredremove; }

public: job(store &_sto,
            proto::eq::eventid __refreshedat,
            const jobname &_name)
    : sto(_sto),
      name(_name),
      _refreshedat(__refreshedat),
      _liststreams(NULL),
      _liststreamssub(Nothing),
      _liststreamsres(Nothing),
      _content(),
      _deferredremove() {}

public: stream *findstream(const streamname &sn, onfilesystemthread);
public: void removestream(stream &, mutex_t::token, onfilesystemthread);
public: void startliststreams(connpool &pool,
                              subscriber &sub,
                              const maybe<streamname> &cursor,
                              mutex_t::token);
public: orerror<void> liststreamswoken(connpool &,
                                       subscriber &,
                                       mutex_t::token,
                                       onfilesystemthread ofs);

public: ~job(); };

/* A remote storage slave. */
class store {
    /* Tag type indicating that you're either in the LISTJOBS call or
     * you've stopped it somehow. */
public: class inlistjobscall {
    private: inlistjobscall() {}
    public:  inlistjobscall(connpool::connlock) {}
    public:  static inlistjobscall mk(onfilesystemthread) {
        return inlistjobscall(); } };
public: const slavename name;
    /* At all times we're either connected or trying to connect. */
public: either<nnp<eqclient<proto::storage::event> >,
               nnp<eqclient<proto::storage::event>::asyncconnect> > _eventqueue;
public: decltype(_eventqueue) &eventqueue(onfilesystemthread) {
    return _eventqueue;}

    /* Connects the filesystem thread subscriber to the eqclient, if
     * we're ready to take events from it, or the asyncconnect, if
     * we're still trying to connect.  Only ever Nothing transiently
     * while we're switching state. */
public: maybe<subscription> _eqsub;
public: maybe<subscription> &eqsub(onfilesystemthread) { return _eqsub; }

    /* LISTJOBs machine state. */
    /* We may have a LISTJOBS outstanding, if we're trying to recover
     * from having lost our event queue connection. */
public: connpool::asynccall *_listjobs;
public: connpool::asynccall *&listjobs(onfilesystemthread) { return _listjobs; }
    /* And a subscription for that. */
public: maybe<subscription> _listjobssub;
public: maybe<subscription> &listjobssub(onfilesystemthread) {
    return _listjobssub; }
    /* Result of the last LISTJOBS to complete, until the filesystem
     * thread collects it. */
public: maybe<proto::storage::listjobsres> _listjobsres;
public: maybe<proto::storage::listjobsres> &listjobsres(
    inlistjobscall) { return _listjobsres; }

public: inlistjobscall abortlistjobscall(onfilesystemthread);

public: list<job> _jobs;
public: list<job> &jobs(mutex_t::token) { return _jobs; }
public: const list<job> &jobs(mutex_t::token) const { return _jobs; }

    /* Jobs which were removed while a LISTJOBS was outstanding.
     * Re-check them all when the LISTJOBS completes. */
public: list<pair<proto::eq::eventid, jobname> > _deferredremove;
public: list<pair<proto::eq::eventid, jobname> > &deferredremove(
    onfilesystemthread) { return _deferredremove; }

public: store(
    subscriber &sub,
    eqclient<proto::storage::event>::asyncconnect &_conn,
    const slavename &_name)
        : name(_name),
          _eventqueue(right<nnp<eqclient<proto::storage::event> > >(
                          _nnp(_conn))),
          _eqsub(Nothing),
          _listjobs(NULL),
          _listjobssub(Nothing),
          _listjobsres(Nothing),
          _jobs(),
          _deferredremove() {
    _eqsub.mkjust(sub, _conn.pub(), SUBSCRIPTION_EQ); }

public: void reconnect(connpool &pool,
                       subscriber &sub,
                       mutex_t::token tok,
                       onfilesystemthread oft);

public: job *findjob(const jobname &jn, mutex_t::token);
public: void dropjob(job &j, mutex_t::token);

public: void eqnewjob(proto::eq::eventid eid,
                      const jobname &job,
                      mutex_t::token);
public: void eqremovejob(proto::eq::eventid eid,
                         const jobname &jn,
                         mutex_t::token,
                         onfilesystemthread);
public: void eqnewstream(proto::eq::eventid eid,
                         const jobname &jn,
                         const streamname &sn,
                         mutex_t::token,
                         onfilesystemthread);
public: void eqfinishstream(proto::eq::eventid eid,
                            const jobname &jn,
                            const streamname &sn,
                            const streamstatus &status,
                            mutex_t::token,
                            onfilesystemthread);
public: void eqremovestream(proto::eq::eventid eid,
                            const jobname &jn,
                            const streamname &sn,
                            mutex_t::token,
                            onfilesystemthread);

public: void eqevent(connpool &,
                     subscriber &,
                     mutex_t::token,
                     onfilesystemthread);
public: orerror<void> eqconnected(connpool &pool,
                                  subscriber &sub,
                                  mutex_t::token,
                                  onfilesystemthread);
public: orerror<void> eqwoken(connpool &,
                              subscriber &,
                              mutex_t::token,
                              onfilesystemthread);

public: void startlistjobs(connpool &pool,
                           subscriber &sub,
                           const maybe<jobname> &cursor,
                           onfilesystemthread oft,
                           inlistjobscall);
public: orerror<void> listjobswoken(connpool &,
                                    subscriber &,
                                    mutex_t::token,
                                    onfilesystemthread oft);

public: ~store(); };

/* Find a stream in a job by name.  Returns NULL if the stream isn't
 * present. */
stream *
job::findstream(const streamname &sn, onfilesystemthread oft) {
    for (auto it(content_unsafe(oft).start()); !it.finished(); it.next()) {
        if (it->status(oft).name() == sn) return &*it; }
    return NULL; }

/* Remove a stream from the job.  Error if it isn't present. */
void
job::removestream(stream &s, mutex_t::token tok, onfilesystemthread ofs) {
    for (auto it(content(tok, ofs).start()); true; it.next()) {
        if (&s == &*it) {
            it.remove();
            return; } } }

/* Start a LISTSTREAMS call.  cursor should be either Nothing, to
 * start at the beginning, or the end marker of the last call, to
 * continue that one. */
void
job::startliststreams(connpool &pool,
                      subscriber &sub,
                      const maybe<streamname> &cursor,
                      mutex_t::token tok) {
    assert(liststreams(tok) == NULL);
    assert(liststreamssub(tok) == Nothing);
    liststreams(tok) = pool.call(
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
        [this] (deserialise1 &ds, connpool::connlock cl) {
            assert(liststreamsres(cl) == Nothing);
            liststreamsres(cl).mkjust(ds);
            return ds.status(); });
    liststreamssub(tok).mkjust(sub,
                               liststreams(tok)->pub(),
                               SUBSCRIPTION_LISTSTREAMS); }

orerror<maybe<job::inliststreamscall> >
job::endliststreamscall(mutex_t::token tok) {
    assert(liststreamssub(tok) != Nothing);
    auto t(liststreams(tok)->finished());
    if (t == Nothing) return maybe<job::inliststreamscall>(Nothing);
    liststreamssub(tok) = Nothing;
    auto r(liststreams(tok)->pop(t.just()));
    liststreams(tok) = NULL;
    if (r.isfailure()) {
        logmsg(loglevel::failure,
               "LISTSTREAMS on " + fields::mk(sto.name) +
               "::" + fields::mk(name) + ": " +
               fields::mk(r.failure()));
        return r.failure(); }
    /* If the LISTSTREAMS call is finished then the filesystem thread
     * can safely pretend to be the LISTSTREAMS call. */
    return mkjust(inliststreamscall::mk(tok)); }

job::inliststreamscall
job::abortliststreamscall(mutex_t::token tok) {
    if (liststreams(tok) != NULL) {
        assert(liststreamssub(tok) != Nothing);
        liststreamssub(tok) = Nothing;
        /* Don't care about result of this; we're already restarting
         * from the beginning. */
        (void)liststreams(tok)->abort();
        liststreams(tok) = NULL; }
    return inliststreamscall::mk(tok); }

/* Called whenever a LISTSTREAMS call's state changes.  If this
 * returns an error then we'll give up on the job. */
orerror<void>
job::liststreamswoken(connpool &pool,
                      subscriber &sub,
                      mutex_t::token tok,
                      onfilesystemthread ofs) {
    auto t(endliststreamscall(tok));
    if (t.isfailure()) return t.failure();
    if (t.success() == Nothing) return Success;
    inliststreamscall isc(t.success().just());
    assert(liststreamsres(isc) != Nothing);
    const auto &result(liststreamsres(isc).just());
    /* Remove any streams which have died. */
    bool lost;
    for (auto it(content(tok, ofs).start());
         !it.finished();
         lost ? it.remove() : it.next()) {
        lost = false;
        /* Ignore streams which aren't in the results range. */
        if (result.start == Nothing ||
            it->status(tok).name() < result.start.just()) continue;
        if (result.end == Nothing ||
            it->status(tok).name() >= result.end.just()) continue;
        /* Ignore streams where our existing knowledge is more up to date
         * than the new result. */
        if (it->refreshedat(ofs) >= result.when) continue;
        /* If the stream isn't in the result set then it's been lost. */
        lost = true;
        for (auto it2(result.res.start()); lost && !it2.finished(); it2.next()){
            lost = it->status(tok).name() != it2->name(); }
        if (lost) {
            /* The common case is that lost streams are detected by
             * event queue messages, so this should be pretty rare,
             * but it can sometimes happen if we lose our EQ
             * subscription and have to recover. */
            logmsg(loglevel::info,
                   "lost stream " + fields::mk(name) +
                   "::" + fields::mk(it->status(tok).name()) +
                   " on " + fields::mk(sto.name) +
                   " to LISTSTREAMS result " + fields::mk(result)); } }
    /* Integrate the result streams into our cache. */
    for (auto it(result.res.start()); !it.finished(); it.next()) {
        auto s(findstream(it->name(), ofs));
        if (s != NULL) {
            /* Already know about this stream.  If what we know is
             * more up to date than the new data we can ignore it. */
            if (s->refreshedat(ofs) < result.when) {
                logmsg(loglevel::debug,
                       "refreshed stream " + fields::mk(sto.name) +
                       "::" + fields::mk(name) +
                       " -> " + fields::mk(s->status(ofs)) +
                       " at " + fields::mk(s->refreshedat(ofs)) +
                       " to " + fields::mk(*it) +
                       " at " + fields::mk(result.when));
                s->status(tok, ofs) = *it;
                s->refreshedat(ofs) = result.when; } }
        else {
            /* New stream -> create it. */
            logmsg(loglevel::debug,
                   "discovered stream " + fields::mk(name) +
                   "::" + fields::mk(*it) +
                   " on " + fields::mk(sto.name) +
                   " at " + fields::mk(result.when));
            content(tok, ofs).append(result.when, *it); } }
    /* Flush the deferred events queue. */
    while (!deferredremove(ofs).empty()) {
        auto r(deferredremove(ofs).pophead());
        sto.eqremovestream(r.first(), name, r.second(), tok, ofs); }
    /* If the server truncated the results then kick off another call
     * to get the rest. */
    auto next(result.end);
    liststreamsres(isc) = Nothing;
    if (next != Nothing) startliststreams(pool, sub, next, tok);
    return Success; }

job::~job() {
    if (_liststreams) {
        _liststreamssub = Nothing;
        _liststreams->abort(); }
    assert(_liststreamssub == Nothing); }

store::inlistjobscall
store::abortlistjobscall(onfilesystemthread oft) {
    if (listjobs(oft) != NULL) {
        assert(listjobssub(oft) != Nothing);
        listjobssub(oft) = Nothing;
        (void)listjobs(oft)->abort();
        listjobs(oft) = NULL; }
    else assert(listjobssub(oft) == Nothing);
    /* No outstanding LISTJOBS -> can allocate an inlistjobs token. */
    return inlistjobscall::mk(oft); }

/* Reconnect to the event queue, tearing down any existing partial
 * connections first. */
/* Note that this only tears down the ``active'' bits of the
 * filesystem i.e. the EQ and outstanding RPC calls.  The cache itself
 * remains intact (so the reconnect is transparent to our callers,
 * modulo the cache not updating while we're working). */
void
store::reconnect(connpool &pool,
                 subscriber &sub,
                 mutex_t::token tok,
                 onfilesystemthread oft) {
    for (auto j(jobs(tok).start()); !j.finished(); j.next()) {
        auto isc(j->abortliststreamscall(tok));
        j->liststreamsres(isc) = Nothing; }
    auto ijc(abortlistjobscall(oft));
    listjobsres(ijc) = Nothing;
    eqsub(oft) = Nothing;
    if (eventqueue(oft).isleft()) eventqueue(oft).left()->destroy();
    else eventqueue(oft).right()->abort();
    eventqueue(oft).mkright(eqclient<proto::storage::event>::connect(
                                pool,
                                name,
                                proto::eq::names::storage));
    eqsub(oft).mkjust(sub, eventqueue(oft).right()->pub(), SUBSCRIPTION_EQ); }

/* Find a job by name.  Returns NULL if the job doesn't exist. */
job *
store::findjob(const jobname &jn, mutex_t::token tok) {
    for (auto it(jobs(tok).start()); !it.finished(); it.next()) {
        if (it->name == jn) return &*it; }
    return NULL; }

/* Remove a job from the job list.  Error if the job does not
 * exist. */
void
store::dropjob(job &j, mutex_t::token tok) {
    for (auto it(jobs(tok).start()); true; it.next()) {
        if (&j == &*it) {
            it.remove();
            return; } } }

/* Process an event queue newjob event. */
void
store::eqnewjob(proto::eq::eventid eid,
                const jobname &job,
                mutex_t::token tok) {
    /* Check whether we've already got the job in the list. */
    auto j(findjob(job, tok));
    if (j == NULL) {
        /* New job. */
        logmsg(loglevel::info,
               "discover new job " + fields::mk(job) +
               " from event at " + fields::mk(eid));
        jobs(tok).append(*this, eid, job);
        /* No LISTSTREAMS: if we saw the newjob event, we will also
         * see all the newstream ones. */ }
    else {
        /* Job already exists.  Mostly a no-op, except that once we've
         * gotten the t_newjob event we're guaranteed to get
         * t_newstream events for any streams in the job, so we can
         * cancel any outstanding LISTSTREAMS. */
        auto isc(j->abortliststreamscall(tok));
        j->liststreamsres(isc) = Nothing; } }

/* Process an event queue removejob event. */
void
store::eqremovejob(proto::eq::eventid eid,
                   const jobname &job,
                   mutex_t::token tok,
                   onfilesystemthread oft) {
    auto j(findjob(job, tok));
    if (j != NULL) {
        /* If our cache is more up to date than this event then it
         * must be a stale event. */
        if (j->refreshedat(oft) > eid) {
            logmsg(loglevel::debug,
                   "drop stale removejob event " + fields::mk(job) +
                   " at " + fields::mk(eid) +
                   "; cache at " + fields::mk(j->refreshedat(oft)));
            return; }
        /* Otherwise, the job is dead and we need to remove it from
         * the cache. */
        logmsg(loglevel::info,
               "drop job " + fields::mk(job) +
               " because of removejob event at " + fields::mk(eid));
        dropjob(*j, tok);
        /* Fall through to the reorder-with-LISTJOBS handling, because
         * that's generally a lot easier to understand. */ }
    else {
        logmsg(loglevel::debug,
               "cannot find job to remove: " + fields::mk(job)); }
    if (listjobs(oft) != NULL) {
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
        deferredremove(oft).append(mkpair(eid, job)); } }

/* Process an event queue newstream event. */
void
store::eqnewstream(proto::eq::eventid eid,
                   const jobname &jn,
                   const streamname &sn,
                   mutex_t::token tok,
                   onfilesystemthread oft) {
    auto j(findjob(jn, tok));
    if (j == NULL) {
        /* Drop newstream events for jobs we don't know about.  If the
         * job is still live then we'll eventually find out about it
         * and recover that way, and if it's been removed then we
         * don't need to do anything at all. */
        logmsg(loglevel::debug,
               "drop new stream event " + fields::mk(jn) +
               "::" + fields::mk(sn) +
               " at " + fields::mk(eid));
        return; }
    auto s(j->findstream(sn, oft));
    if (s != NULL) {
        /* We already know about this stream, so the newstream event
         * must have been delayed. */
        if (eid > s->refreshedat(oft)) {
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
                   "; was " + fields::mk(s->status(oft)) +
                   " at " + fields::mk(s->refreshedat(oft)));
            /* Update the cached liveness and hope for the best. */
            s->refreshedat(oft) = eid; }
        else {
            /* We already knew it was live at time T and we were just
             * told it was created at time T-n.  That's fine. */
            logmsg(loglevel::debug,
                   "drop old new stream event " + fields::mk(jn) +
                   "::" + fields::mk(sn) +
                   " at " + fields::mk(eid) +
                   "; cache at " + s->refreshedat(oft).field()); } }
    else {
        /* Fresh stream.  Add it to the job. */
        logmsg(loglevel::info,
               "new stream " + fields::mk(jn) +
               "::" + fields::mk(sn) +
               " at " + fields::mk(eid));
        j->content(tok, oft).append(eid, streamstatus::empty(sn)); } }

/* Process an event queue finish stream event. */
void
store::eqfinishstream(proto::eq::eventid eid,
                      const jobname &jn,
                      const streamname &sn,
                      const streamstatus &status,
                      mutex_t::token tok,
                      onfilesystemthread oft) {
    auto j(findjob(jn, tok));
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
    auto s(j->findstream(sn, oft));
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
    if (s->refreshedat(oft) >= eid) {
        /* This is an obsolete event.  Drop it. */
        logmsg(loglevel::info,
               "drop finish stream event " + fields::mk(jn) +
               "::" + fields::mk(sn) +
               " -> " + fields::mk(status) +
               " at " + fields::mk(eid) +
               "; cache " + fields::mk(s->status(oft)) +
               " at " + fields::mk(s->refreshedat(oft)));
        return; }
    if (s->status(oft).isfinished()) {
        /* We knew the stream was finished at or before T, and we're
         * now being told it only get finished at T+n.  That implies
         * we've had a rewind. */
        /* Another way of looking at: streams can only be finished
         * twice with an intervening remove event, but if we'd seen
         * the remove the stream wouldn't be in the cache any more, so
         * we must have missed a remove. */
        logmsg(loglevel::error,
               "detected inconsistency in stream lifecycle"
               " from finish stream event " + fields::mk(jn) +
               "::" + fields::mk(sn) +
               " -> " + fields::mk(status) +
               " at " + fields::mk(eid) +
               "; cache " + fields::mk(s->status(oft)) +
               " at " + fields::mk(s->refreshedat(oft)));
        /* apply it anyway. */ }
    /* This is the finish of this stream. */
    s->status(tok, oft) = status;
    s->refreshedat(oft) = eid; }

/* Process an event queue removestream event. */
void
store::eqremovestream(proto::eq::eventid eid,
                      const jobname &jn,
                      const streamname &sn,
                      mutex_t::token tok,
                      onfilesystemthread oft) {
    auto j(findjob(jn, tok));
    if (j == NULL) {
        /* If we don't have the job then we don't have any of its
         * streams and we don't have any LISTSTREAMS for it, so we're
         * fine. */
        logmsg(loglevel::debug,
               "remove stream " + fields::mk(jn) +
               "::" + fields::mk(sn) +
               " at " + fields::mk(eid) +
               " but job is missing");
        return; }
    {   auto s(j->findstream(sn, oft));
        if (s == NULL) {
            logmsg(loglevel::debug,
                   "remove stream " + fields::mk(jn) +
                   "::" + fields::mk(sn) +
                   " at " + fields::mk(eid) +
                   " but it's already gone"); }
        else {
            if (s->refreshedat(oft) >= eid) {
                /* Already know stream alive at time T, so can safely
                 * ignore message that it was removed at T-n, because
                 * it was reconstructed and the remove message got
                 * delayed after a LISTSTREAMS. */
                logmsg(loglevel::debug,
                       "drop remove stream " + fields::mk(jn) +
                       "::" + fields::mk(sn) +
                       " at " + fields::mk(eid) +
                       " because stream live at " +
                       fields::mk(s->refreshedat(oft)));
                return; }
            /* This is the actual remove operation. */
            j->removestream(*s, tok, oft); } }
    /* Need to make sure the remove isn't reordered with the discovery
     * of the stream in a LISTSTREAMS, to avoid resurrection bugs. */
    if (j->liststreams(tok) != NULL) {
        j->deferredremove(oft).append(mkpair(eid, sn));
        logmsg(loglevel::debug,
               "defer remove " + fields::mk(jn) +
               "::" + fields::mk(sn) +
               " at " + fields::mk(eid) +
               " because of outstanding LISTSTREAMS"); }
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
store::eqevent(connpool &pool,
               subscriber &sub,
               mutex_t::token tok,
               onfilesystemthread oft) {
    auto ev(eventqueue(oft).left()->popid());
    if (ev == Nothing) return;
    if (ev.just().isfailure()) {
        /* Error on the event queue.  Tear it down, abort all
         * outstanding calls, and restart the machinery from the
         * beginning. */
        logmsg(loglevel::failure,
               "lost eq to " + fields::mk(name) +
               ": " + fields::mk(ev.just().failure()) +
               ". Reconnect.");
        return reconnect(pool, sub, tok, oft); }
    auto eid(ev.just().success().first());
    auto &evt(ev.just().success().second());
    switch (evt.typ) {
    case proto::storage::event::t_newjob:
        return eqnewjob(eid, evt.job, tok);
    case proto::storage::event::t_removejob:
        return eqremovejob(eid, evt.job, tok, oft);
    case proto::storage::event::t_newstream:
        return eqnewstream(eid, evt.job, evt.stream.just(), tok, oft);
    case proto::storage::event::t_finishstream:
        return eqfinishstream(eid,
                              evt.job,
                              evt.stream.just(),
                              evt.status.just(),
                              tok,
                              oft);
    case proto::storage::event::t_removestream:
        return eqremovestream(eid, evt.job, evt.stream.just(), tok, oft); }
    abort(); }

/* Check if a store's finished connecting to its event queue.  If the
 * EQ connect fails then we'll drop the store. */
orerror<void>
store::eqconnected(connpool &pool,
                   subscriber &sub,
                   mutex_t::token tok,
                   onfilesystemthread oft) {
    /* Shouldn't have LISTJOBS or LISTSTREAMS calls outstanding if we
     * don't have a queue. */
    assert(listjobs(oft) == NULL);
    for (auto it(jobs(tok).start()); !it.finished(); it.next()) {
        assert(it->liststreams(tok) == NULL); }
    auto t(eventqueue(oft).right()->finished());
    if (t == Nothing) return Success;
    eqsub(oft) = Nothing;
    auto res(eventqueue(oft).right()->pop(t.just()));
    if (res.isfailure()) {
        logmsg(loglevel::failure,
               "cannot connect to " + fields::mk(name) +
               " event queue: " + fields::mk(res.failure()));
        /* Give up on this one for now. */
        /* XXX we'll retry when the beacon next wakes us up, which is
         * unlikely to be the optimal strategy for this kind of
         * failure. */
        return res.failure(); }
    eventqueue(oft).mkleft(res.success());
    eqsub(oft).mkjust(sub, eventqueue(oft).left()->pub(), SUBSCRIPTION_EQ);
    
    /* We may have missed some newjob events.  Start a LISTJOBS
     * machine to regenerate them. */
    /* inlistjobscall::mk safe because we've stopped the LISTJOBS
     * call. */
    startlistjobs(pool, sub, Nothing, oft, inlistjobscall::mk(oft));
    
    /* If we have any jobs then we may also have missed some stream
     * events.  Start LISTSTREAMS machines for all of the jobs to
     * catch up again. */
    for (auto j(jobs(tok).start()); !j.finished(); j.next()) {
        j->startliststreams(pool, sub, Nothing, tok); }
    return Success; }

/* Event queue changed, either by completing the connect or by
 * receiving another event. */
orerror<void>
store::eqwoken(connpool &pool,
               subscriber &sub,
               mutex_t::token tok,
               onfilesystemthread oft) {
    if (eventqueue(oft).isright()) return eqconnected(pool, sub, tok, oft);
    else {
        eqevent(pool, sub, tok, oft);
        return Success; } }

/* Start a LISTJOBS call.  Start from the beginning if the cursor is
 * Nothing, otherwise restart from where we left off. */
void
store::startlistjobs(connpool &pool,
                     subscriber &sub,
                     const maybe<jobname> &cursor,
                     onfilesystemthread oft,
                     inlistjobscall) {
    assert(listjobs(oft) == NULL);
    assert(listjobssub(oft) == Nothing);
    listjobs(oft) = pool.call(
        name,
        interfacetype::storage,
        /* Infinite timeout is safe: we'll give up when it drops out
         * of the beacon client. */
        Nothing,
        [cursor] (serialise1 &s, connpool::connlock) {
            s.push(proto::storage::tag::listjobs);
            s.push(cursor);
            s.push(maybe<unsigned>(Nothing)); },
        [this] (deserialise1 &ds, connpool::connlock cl) {
            assert(listjobsres(cl) == Nothing);
            listjobsres(cl).mkjust(ds);
            return ds.status(); });
    listjobssub(oft).mkjust(sub, listjobs(oft)->pub(), SUBSCRIPTION_LISTJOBS); }

/* Called whenever something might have happened to a store LISTJOBS
 * call.  Responsible for checking whether it's finished, and, if so,
 * processing the results and potentially starting another call.  If
 * this returns an error then we'll drop the store from the list. */
orerror<void>
store::listjobswoken(connpool &pool,
                     subscriber &sub,
                     mutex_t::token tok,
                     onfilesystemthread oft) {
    assert(listjobssub(oft) != Nothing);
    assert(listjobs(oft) != NULL);
    {   auto t(listjobs(oft)->finished());
        if (t == Nothing) return Success;
        listjobssub(oft) = Nothing;
        auto r(listjobs(oft)->pop(t.just()));
        listjobs(oft) = NULL;
        if (r.isfailure()) {
            logmsg(loglevel::failure,
                   "LISTJOBS on " + name.field() +
                   ": " + r.failure().field());
            return r.failure(); } }
    /* Safe because we've stopped the call. */
    auto ijc(inlistjobscall::mk(oft));
    assert(listjobsres(ijc) != Nothing);
    auto &result(listjobsres(ijc).just());
    /* Remove any jobs which have died. */
    bool found;
    for (auto job(jobs(tok).start());
         !job.finished();
         found ? job.next() : job.remove()) {
        found = true;
        /* Ignore jobs which aren't in the results range. */
        if (result.start == Nothing ||
            job->name < result.start.just()) continue;
        if (result.end == Nothing || job->name >= result.end.just()) continue;
        /* Ignore jobs where our existing knowledge is more up to date
         * than the new result. */
        if (job->refreshedat(oft) >= result.when) continue;
        /* If the job isn't in the result set then it's been lost. */
        found = false;
        for (auto jn(result.res.start());
             !found && !jn.finished();
             jn.next()){
            found = job->name == *jn; }
        if (!found) {
            /* The common case is that lost jobs are detected by event
             * queue messages, so this should be pretty rare, but it
             * can sometimes happen if we lose our EQ subscription and
             * have to recover. */
            logmsg(loglevel::info,
                   "lost job " + fields::mk(job->name) + " on " +
                   fields::mk(name) + " to LISTJOBS result " +
                   fields::mk(result)); } }
    /* Integrate any new jobs into the list. */
    for (auto jn(result.res.start()); !jn.finished(); jn.next()) {
        auto j(findjob(*jn, tok));
        if (j != NULL) {
            /* Already have it, so just update the liveness
             * timestamp. */
            if (j->refreshedat(oft) < result.when) {
                j->refreshedat(oft) = result.when; } }
        else {
            /* New job -> create it and start looking for streams in
             * it. */
            logmsg(loglevel::debug,
                   "discovered job " + fields::mk(*jn) + " on " +
                   fields::mk(name));
            jobs(tok).append(*this, result.when, *jn)
                .startliststreams(pool,
                                  sub,
                                  Nothing,
                                  tok); } }
    /* Flush the deferred events queue. */
    while (!deferredremove(oft).empty()) {
        auto r(deferredremove(oft).pophead());
        eqremovejob(r.first(), r.second(), tok, oft); }
    /* If the server truncated the results then kick off another call
     * to get the rest. */
    auto next(result.end);
    listjobsres(ijc) = Nothing;
    if (next != Nothing) startlistjobs(pool, sub, next, oft, ijc);
    return Success; }

/* Store destructor.  This can be called when the store is any state,
 * so it must do things like cancelling outstanding async calls.  Only
 * called from the filesystem thread. */
store::~store() {
    onfilesystemthread oft;
    eqsub(oft) = Nothing;
    if (eventqueue(oft).isleft()) eventqueue(oft).left()->destroy();
    else eventqueue(oft).right()->abort();
    listjobssub(oft) = Nothing;
    if (listjobs(oft) != NULL) listjobs(oft)->abort(); }

} /* End of namespace filesystemimpl */

using namespace filesystemimpl;
class filesystem::impl : public thread {
public:  filesystem api;
private: connpool &pool;
private: beaconclient &bc;
private: waitbox<void> shutdown;

    /* Protects pretty much all of our interesting fields. */
public:  mutable mutex_t mux;

    /* All of the stores which the beacon's told us about. */
public:  list<store> _stores;
public:  list<store> &stores(mutex_t::token) { return _stores; }
public:  const list<store> &stores(mutex_t::token) const { return _stores; }

public:  explicit impl(const thread::constoken &token,
                       connpool &_pool,
                       beaconclient &_bc)
    : thread(token),
      pool(_pool),
      bc(_bc),
      shutdown(),
      mux(),
      _stores() {}

public:  void run(clientio);

public:  void beaconwoken(subscriber &sub, mutex_t::token tok);

public:  void dropstore(store &, mutex_t::token);

public:  list<slavename> findjob(const jobname &jn) const;
public:  list<pair<slavename, streamstatus> > findstream(
    const jobname &jn,
    const streamname &sn) const; };

void
filesystem::impl::run(clientio io) {
    onfilesystemthread oft;
    
    subscriber sub;
    subscription bcsub(sub, bc.changed());
    subscription sssub(sub, shutdown.pub);
    /* Force an initial beacon scan. */
    bcsub.set();
    while (!shutdown.ready()) {
        auto s(sub.wait(io));
        if (s == &sssub) continue;
        auto token(mux.lock());
        if (s == &bcsub) beaconwoken(sub, token);
        else if (s->data == SUBSCRIPTION_EQ) {
            auto sto(containerof(static_cast<subscription *>(s),
                                 store,
                                 eqsub(oft).__just()));
            auto res(sto->eqwoken(pool, sub, token, oft));
            if (res.isfailure()) dropstore(*sto, token); }
        else if (s->data == SUBSCRIPTION_LISTJOBS) {
            auto sto(containerof(static_cast<subscription *>(s),
                                 store,
                                 listjobssub(oft).__just()));
            auto res(sto->listjobswoken(pool, sub, token, oft));
            if (res.isfailure()) dropstore(*sto, token); }
        else if (s->data == SUBSCRIPTION_LISTSTREAMS) {
            auto j(containerof(static_cast<subscription *>(s),
                               filesystemimpl::job,
                               liststreamssub(token).__just()));
            auto res(j->liststreamswoken(pool, sub, token, oft));
            if (res.isfailure()) j->sto.dropjob(*j, token); }
        else abort();
        mux.unlock(&token); }
    /* The stores might have references to our subscriber block, so
     * they must be flushed before we return. */
    mux.locked([this] (mutex_t::token tok) { stores(tok).flush(); }); }

/* Helper function which gets called whenever there's any possibility
 * that the beacon content has changed.  Compare the beacon results to
 * the cache and update the cache as appropriate. */
void
filesystem::impl::beaconwoken(subscriber &sub, mutex_t::token tok) {
    logmsg(loglevel::debug, "scan for new storage slaves");
    list<list<store>::iter> lost;
    for (auto it(stores(tok).start()); !it.finished(); it.next()) {
        lost.pushtail(it); }
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
            stores(tok).append(sub,
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
filesystem::impl::dropstore(store &s, mutex_t::token tok) {
    for (auto it(stores(tok).start()); true; it.next()) {
        if (&s == &*it) {
            it.remove();
            return; } } }

/* Find all of the slaves which know anything about a particular job. */
list<slavename>
filesystem::impl::findjob(const jobname &jn) const {
    list<slavename> res;
    auto tok(mux.lock());
    for (auto sto(stores(tok).start()); !sto.finished(); sto.next()) {
        for (auto job(sto->jobs(tok).start());
             !job.finished();
             job.next()) {
            if (job->name == jn) {
                res.append(sto->name);
                break; } } }
    mux.unlock(&tok);
    return res; }

/* Find all of our copies of a particular stream. */
list<pair<slavename, streamstatus> >
filesystem::impl::findstream(const jobname &jn, const streamname &sn) const {
    list<pair<slavename, streamstatus> > res;
    auto token(mux.lock());
    for (auto sto(stores(token).start()); !sto.finished(); sto.next()) {
        for (auto job(sto->jobs(token).start());
             !job.finished();
             job.next()) {
            if (job->name != jn) continue;
            for (auto stream(job->content(token).start());
                 !stream.finished();
                 stream.next()) {
                if (stream->status(token).name() != sn) continue;
                res.append(sto->name, stream->status(token)); } } }
    mux.unlock(&token);
    return res; }

const filesystem::impl &
filesystem::implementation() const {
    return *containerof(this, const filesystem::impl, api); }

filesystem::impl &
filesystem::implementation() {
    return *containerof(this, filesystem::impl, api); }

/* Construct a new filesystem which will keep track of all stores
 * known to the beacon client. */
filesystem &
filesystem::build(connpool &_pool, beaconclient &_bc) {
    return thread::start<filesystem::impl>(fields::mk("filesystem"), _pool, _bc)
        ->api; }

list<slavename>
filesystem::findjob(const jobname &jn) const {
    return implementation().findjob(jn); }

list<pair<slavename, streamstatus> >
filesystem::findstream(const jobname &jn, const streamname &sn) const {
    return implementation().findstream(jn, sn); }
