#include "eqclient.H"

#include "buffer.H"
#include "connpool.H"
#include "fields.H"
#include "nnp.H"
#include "pair.H"
#include "agentname.H"
#include "test.H"
#include "thread.H"
#include "util.H"

#include "connpool.tmpl"
#include "list.tmpl"
#include "maybe.tmpl"
#include "mutex.tmpl"
#include "pair.tmpl"
#include "test.tmpl"
#include "thread.tmpl"

#define CLIENT geneqclient::impl
#define CONNECT geneqclient::asyncconnect::impl

class CLIENT : public thread {
public: geneqclient api;
public: connpool &pool;
public: const agentname agent;
public: const proto::eq::genname queuename;
public: const proto::eq::subscriptionid subid;
public: const eqclientconfig config;

public: mutex_t mux;
public: orerror<void> _status;
public: orerror<void> &status(mutex_t::token) { return _status; }
public: list<pair<proto::eq::eventid, buffer> > _queue;
public: list<pair<proto::eq::eventid, buffer> > &queue(
    mutex_t::token) { return _queue; }

    /* Notified when more stuff gets added to the queue. */
public: publisher pub;

public: waitbox<void> shutdown;

    /* Tag for things only invoked on the queue thread. */
public: class onqueuethread {};
    /* The next thing to try to get. */
public: proto::eq::eventid _cursor;
public: proto::eq::eventid &cursor(onqueuethread) { return _cursor; }
    /* The next thing to trim, or Nothing if we don't need a trim. */
    /* Synchronisation: only changed from the getter completion method
     * and only read from the queue thread when the getter
     * completes. */
public: maybe<proto::eq::eventid> trim;

public: impl(const constoken &,
             connpool &_pool,
             const agentname &_agent,
             proto::eq::subscriptionid _subid,
             proto::eq::genname _name,
             proto::eq::eventid _cursor,
             const eqclientconfig &config);
public: nnp<connpool::asynccallT<bool> > startgetter(onqueuethread);
public: nnp<connpool::asynccall> startwaiter(onqueuethread);
public: void addeventtoqueue(proto::eq::eventid,
                             buffer &buf,
                             connpool::connlock);
public: void failqueue(error);
public: void run(clientio);
public: ~impl(); };

class CONNECT {
public: geneqclient::asyncconnect api;
public: typedef pair<proto::eq::subscriptionid, proto::eq::eventid> callres_t;
public: connpool::asynccallT<callres_t> &inner;
public: connpool &pool;
public: const agentname agent;
public: const proto::eq::genname queuename;
public: const eqclientconfig config;
public: impl(connpool::asynccallT<callres_t> &,
             connpool &,
             const agentname &,
             const proto::eq::genname &,
             const eqclientconfig &);
public: orerror<pair<nnp<geneqclient>, proto::eq::eventid> > pop(token);
public: void abort(); };

eqclientconfig::eqclientconfig()
    : unsubscribe(timedelta::seconds(1)),
      get(timedelta::seconds(10)),
      wait(timedelta::minutes(1)),
      maxqueue(200) {}

eqclientconfig
eqclientconfig::dflt() { return eqclientconfig(); }

/* --------------------- geneqclient::asyncconnect API ---------------- */
geneqclient::asyncconnect::impl &
geneqclient::asyncconnect::implementation() {
    return *containerof(this, CONNECT, api); }

const geneqclient::asyncconnect::impl &
geneqclient::asyncconnect::implementation() const {
    return *containerof(this, CONNECT, api); }

const publisher &
geneqclient::asyncconnect::pub() const { return implementation().inner.pub(); }

maybe<geneqclient::asyncconnect::token>
geneqclient::asyncconnect::finished() const {
    auto r(implementation().inner.finished());
    if (r == Nothing) return Nothing;
    else return token(r.just()); }

orerror<pair<nnp<geneqclient>, proto::eq::eventid> >
geneqclient::asyncconnect::pop(token t) { return implementation().pop(t); }

void
geneqclient::asyncconnect::abort() { return implementation().abort(); }

/* --------------- geneqclient::asyncconnect implementation ----------- */
CONNECT::impl(
    connpool::asynccallT<callres_t> &_inner,
    connpool &_pool,
    const agentname &_agent,
    const proto::eq::genname &_queuename,
    const eqclientconfig &_config)
    : inner(_inner),
      pool(_pool),
      agent(_agent),
      queuename(_queuename),
      config(_config) {}

orerror<pair<nnp<geneqclient>, proto::eq::eventid> >
CONNECT::pop(token t) {
    auto r(inner.pop(t.inner));
    if (r.isfailure()) {
        delete this;
        return r.failure(); }
    else {
        auto &res(thread::start<CLIENT>(
                     "EC:" + queuename.field() + ":" + fields::mk(agent),
                     pool,
                     agent,
                     r.success().first(),
                     queuename,
                     r.success().second(),
                     config)->api);
        delete this;
        return success(mkpair(_nnp(res), r.success().second().pred())); } }

void
CONNECT::abort() {
    inner.abort();
    delete this; }

/* ---------------------------- geneqclient API ----------------------- */
CLIENT &
geneqclient::implementation() { return *containerof(this, CLIENT, api); }

const CLIENT &
geneqclient::implementation() const { return *containerof(this, CLIENT, api); }

nnp<geneqclient::asyncconnect>
geneqclient::connect(connpool &pool,
                     const agentname &sn,
                     const proto::eq::genname &name,
                     const eqclientconfig &config) {
    using namespace proto::eq;
    auto r(pool.call<CONNECT::callres_t>(
               sn,
               interfacetype::eq,
               Nothing,
               [&name] (serialise1 &s, connpool::connlock) {
                   tag::subscribe.serialise(s);
                   name.serialise(s); },
               [] (deserialise1 &ds, connpool::connlock) {
                   return success(CONNECT::callres_t(ds)); }));
    return _nnp((new CONNECT(*r, pool, sn, name, config))->api); }

orerror<pair<nnp<geneqclient>, proto::eq::eventid> >
geneqclient::connect(clientio io,
                     connpool &pool,
                     const agentname &sn,
                     const proto::eq::genname &name,
                     timestamp deadline,
                     const eqclientconfig &config) {
    using namespace proto::eq;
    auto cc(connect(pool, sn, name, config));
    auto token(cc->finished());
    {   subscriber sub;
        subscription ss(sub, cc->pub());
        while (token == Nothing) {
            if (sub.wait(io, deadline) == NULL) goto timeout;
            token = cc->finished(); } }
    return cc->pop(token.just());
 timeout:
    cc->abort();
    return error::timeout; }

const publisher &
geneqclient::pub() const { return implementation().pub; }

maybe<orerror< pair<proto::eq::eventid, buffer> > >
geneqclient::pop() {
    auto &i(implementation());
    auto tok(i.mux.lock());
    if (i.status(tok).isfailure()) {
        auto err(i.status(tok).failure());
        i.mux.unlock(&tok);
        return orerror<pair<proto::eq::eventid, buffer> >(err); }
    else if (i.queue(tok).empty()) {
        i.mux.unlock(&tok);
        return Nothing; }
    else {
        auto &e(i.queue(tok).peekhead());
        maybe<orerror<pair<proto::eq::eventid, buffer> > > res(
            success(mkpair(e.first(), buffer(Steal, e.second()))));
        i.queue(tok).pophead();
        i.mux.unlock(&tok);
        return res; } }

void
geneqclient::destroy() {
    auto &i(implementation());
    i.shutdown.set();
    /* Make things easy on the server by telling it that we've gone
     * away.  This is optional, and we don't particularly care about
     * whether or not it succeeds.  Note that there's no clientio
     * token here: the unsubscribe timeout is supposed to be short, so
     * we don't need one. */
    i.pool.call(clientio::CLIENTIO,
                i.agent,
                interfacetype::eq,
                i.config.unsubscribe.future(),
                [&i] (serialise1 &s, connpool::connlock) {
                    proto::eq::tag::unsubscribe.serialise(s);
                    i.queuename.serialise(s);
                    i.subid.serialise(s); })
        .warn("unsubscribing from " + i.subid.field());
    /* Should be quick because shutdown is set. */
    i.join(clientio::CLIENTIO); }

geneqclient::~geneqclient() {}

tests::hookpoint<void>
geneqclient::startingwaiter([] {});

/* ------------------------ geneqclient implementation --------------------- */
CLIENT::impl(const constoken &token,
             connpool &_pool,
             const agentname &_agent,
             proto::eq::subscriptionid _subid,
             proto::eq::genname _name,
             proto::eq::eventid __cursor,
             const eqclientconfig &_config)
    : thread(token),
      pool(_pool),
      agent(_agent),
      queuename(_name),
      subid(_subid),
      config(_config),
      mux(),
      _status(Success),
      _queue(),
      pub(),
      shutdown(),
      _cursor(__cursor),
      trim(Nothing) {}

/* The getter is responsible for actually fetching events from the
 * remote system.  It returns true if the event hasn't been generated
 * yet, in which case we need to go back to waiting, or false if it
 * has been, in which case we need to start another getter (after
 * advancing the cursor). */
nnp<connpool::asynccallT<bool> >
CLIENT::startgetter(onqueuethread oqt) {
    auto c(cursor(oqt));
    return pool.call<bool>(
        agent,
        interfacetype::eq,
        config.get.future(),
        [this, c] (serialise1 &s, connpool::connlock) {
            proto::eq::tag::get.serialise(s);
            queuename.serialise(s);
            subid.serialise(s);
            c.serialise(s); },
        [this, c] (deserialise1 &ds, connpool::connlock cl) -> orerror<bool> {
            maybe<pair<proto::eq::eventid, buffer> > res(ds);
            if (res == Nothing) return true;
            else {
                buffer b(Steal, res.just().second());
                addeventtoqueue(res.just().first(), b, cl);
                trim = c;
                return false; } }); }

nnp<connpool::asynccall>
CLIENT::startwaiter(onqueuethread oqt) {
    geneqclient::startingwaiter();
    return pool.call(
        agent,
        interfacetype::eq,
        config.wait.future(),
        [this, oqt] (serialise1 &s, connpool::connlock) {
            proto::eq::tag::wait.serialise(s);
            queuename.serialise(s);
            subid.serialise(s);
            cursor(oqt).serialise(s); }); }

void
CLIENT::addeventtoqueue(proto::eq::eventid evt,
                        buffer &buf,
                        connpool::connlock) {
    mux.locked([&] (mutex_t::token tok) {
            queue(tok).append(mkpair(evt, buffer(Steal, buf)));
            pub.publish(); }); }

void
CLIENT::failqueue(error e) {
    mux.locked([&] (mutex_t::token tok) {
            status(tok) = e;
            pub.publish(); }); }

void
CLIENT::run(clientio io) {
    onqueuethread oqt;
    
    subscriber sub;
    subscription ss(sub, shutdown.pub());
    /* Non-Nothing as long as the getter exists. */
    maybe<subscription> gettersub(Nothing);
    /* Non-Nothing as long as the waiter exists. */
    maybe<subscription> waitersub(Nothing);
    
    /* Two level thing for getting the next event: we have a getter,
     * which is supposed to complete quickly and to always succeed unless
     * the server has failed, and a waiter, which can take a long time
     * and might timeout if the server is live but not currently
     * generating any events. */
    auto getter(&*startgetter(oqt));
    connpool::asynccall *waiter(NULL);
    
    /* Trim state: the outstanding trimmer and the thing it's trying
     * to trim, if we have an outstanding trimmer, or Nothing
     * otherwise. */
    maybe<pair<proto::eq::eventid, nnp<connpool::asynccall> > >
        trimmer(Nothing);
    trimmer.silencecompiler(proto::eq::eventid::compilerdummy(),
                            *(connpool::asynccall *)NULL);
    
    /* The last thing we've successfully trimmed, or Nothing if we
     * haven't trimmed anything yet. */
    maybe<proto::eq::eventid> trimmedto(Nothing);
    trimmedto.silencecompiler(proto::eq::eventid::compilerdummy());
    
    gettersub.mkjust(sub, getter->pub());
    
    while (true) {
        if (shutdown.ready()) break;
        if (getter != NULL) {
            logmsg(loglevel::verbose, "check getter");
            assert(gettersub != Nothing);
            assert(waiter == NULL);
            assert(waitersub == Nothing);
            auto gettertok(getter->finished());
            if (gettertok != Nothing) {
                logmsg(loglevel::verbose, "getter ready");
                gettersub.mknothing();
                auto getterres(getter->pop(gettertok.just()));
                getter = NULL;
                if (trimmer != Nothing) {
                    auto trimmertok(trimmer.just().second()->finished());
                    if (trimmertok.isjust()) {
                        logmsg(loglevel::verbose, "trimmer finished");
                        auto r(trimmer.just().second()->pop(trimmertok.just()));
                        if (r.isfailure()) r.failure().warn("trimming queue");
                        else trimmedto = trimmer.just().first();
                        trimmer = Nothing; } }
                if (trimmer == Nothing &&
                    trim != Nothing &&
                    (trimmedto == Nothing ||
                     trimmedto.just() < trim.just())) {
                    logmsg(loglevel::verbose, "starting trimmer");
                    trimmer =
                        mkpair(
                            trim.just(),
                            _nnp(
                                *pool.call(
                                    agent,
                                    interfacetype::eq,
                                    Nothing,
                                    [this] (serialise1 &s, connpool::connlock) {
                                        proto::eq::tag::trim.serialise(s);
                                        queuename.serialise(s);
                                        subid.serialise(s);
                                        trim.just().serialise(s); }))); }
                if (getterres.isfailure()) {
                    logmsg(loglevel::verbose, "getter failed");
                    failqueue(getterres.failure());
                    break; }
                if (getterres.success()) {
                    /* Server hasn't generated this event yet.  Go to
                     * wait mode. */
                    logmsg(loglevel::verbose,
                           "switch to wait mode; " + _cursor.field() +
                           " not ready yet");
                    waiter = startwaiter(oqt);
                    waitersub.mkjust(sub, waiter->pub());
                    /* Fall through to check for events on the
                     * waiter. */ }
                else {
                    /* Successfully grabbed an event from the remote
                     * system.  Advance the cursor and start another
                     * getter. */
                    logmsg(loglevel::verbose,
                           "grabbed event " + _cursor.field());
                    cursor(oqt)++;
                    /* Artificially limit the size of the backlog,
                     * failing the queue when it overflows.  This is
                     * mostly a safety catch: growing the queue when
                     * nothing's draining (e.g. because the thing
                     * which is supposed to drain it is overloaded) it
                     * is pointless, and it's better to find out that
                     * things are going wrong sooner rather than
                     * later. */
                    if (mux.locked<bool>([&] (mutex_t::token tok) {
                                return queue(tok).length()>config.maxqueue; })){
                        logmsg(loglevel::info,
                               "queue " + queuename.field() + " sub " +
                               subid.field() + " overflowed locally");
                        failqueue(error::eventsdropped);
                        break; }
                    getter = startgetter(oqt);
                    gettersub.mkjust(sub, getter->pub());
                    /* Need to restart to avoid lost wakeups setting
                     * the sub. */
                    continue; } } }
        if (waiter != NULL) {
            logmsg(loglevel::verbose, "check waiter");
            assert(waitersub != Nothing);
            assert(getter == NULL);
            assert(gettersub == Nothing);
            auto waitertok(waiter->finished());
            if (waitertok != Nothing) {
                logmsg(loglevel::verbose, "waiter ready");
                waitersub.mknothing();
                auto waiterres(waiter->pop(waitertok.just()));
                waiter = NULL;
                if (waiterres.issuccess() || waiterres == error::timeout) {
                    /* Go to getter mode. */
                    /* This is obviously necessary when the waiter
                     * succeeds, because that means the getter will
                     * also succeed, but why do we do it for timeouts
                     * as well?  Answer: because that's how we
                     * distinguish a server which can't respond at all
                     * from one which is merely idle. */
                    logmsg(loglevel::verbose, "switch to getter mode");
                    getter = startgetter(oqt);
                    gettersub.mkjust(sub, getter->pub());
                    continue; }
                else {
                    logmsg(loglevel::verbose, "waiter failed");
                    failqueue(waiterres.failure());
                    break; } } }
        /* No work, so go to sleep. */
        sub.wait(io); }
    
    if (trimmer != Nothing) trimmer.just().second()->abort();
    if (gettersub == Nothing) assert(getter == NULL);
    else {
        gettersub.mknothing();
        getter->abort(); }
    if (waitersub == Nothing) assert(waiter == NULL);
    else {
        waitersub.mknothing();
        waiter->abort(); } }

CLIENT::~impl() {}
