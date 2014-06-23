#include "coordinator.H"

#include "controlserver.H"
#include "fields.H"
#include "logging.H"
#include "mastersecret.H"
#include "peername.H"
#include "proto.H"
#include "ratelimiter.H"
#include "registrationsecret.H"
#include "rpcconn.H"
#include "timedelta.H"

#include "rpcserver.tmpl"

class coordinatorimpl;

class coordinatorconn {
/* Each coordinator connection has its own dedicated thread for
   running pings and checking that the connection remains alive.
   That's a little expensive but helps to ensure that one ill-behaved
   worker can't cause excessive interference for the other
   well-behaved ones. */
public: class timerthread : public threadfn {
    private: coordinatorconn *const owner;
    public:  timerthread(coordinatorconn *_owner);
    private: void run();
    };
public: mutex_t mux;
public: int active;
public: publisher idle;
public: waitbox<bool> shutdown;
public: timerthread timerthread_;
public: mutex_t contactmux;
public: timestamp lastcontact;
public: thread *timerthreadhandle;
public: rpcconn &conn;
public: coordinatorimpl *owner;
public: coordinatorconn(rpcconn &_conn, coordinatorimpl *_owner)
    : mux(),
      active(0),
      idle(),
      shutdown(),
      timerthread_(this),
      contactmux(),
      lastcontact(timestamp::now()),
      timerthreadhandle(NULL),
      conn(_conn),
      owner(_owner) {}
private: void contact(); };

class coordinatorimpl : public coordinator {
private: mastersecret ms;
private: ratelimiter newconnlimiter;
private: registrationsecret rs;

    /* Control server interface */
private: class statusinterface : public rpcinterface<controlconn *> {
    public:  statusinterface() : rpcinterface(proto::COORDINATORSTATUS::tag) {}
    private: maybe<error> message(const wireproto::rx_message &,
                                  controlconn *,
                                  buffer &);
    };
private: statusinterface statusiface;
private: rpcregistration<controlconn *> *controlregistration;

private: mutex_t mux;
private: list<coordinatorconn *> connections;
public:  coordinatorimpl(const mastersecret &ms,
                         const registrationsecret &rs,
                         controlserver *cs);
private: orerror<coordinatorconn *> startconn(rpcconn &);
private: void endconn(coordinatorconn *);
private: void destroy();
};

coordinatorconn::timerthread::timerthread(
    coordinatorconn *_owner)
    : owner(_owner) {}

void
coordinatorconn::timerthread::run() {
    subscriber sub;
    subscription shutdownsub(sub, owner->shutdown.pub);
    auto nextping(timestamp::now() + timedelta::seconds(1));
    bool finished = false;
    while (!finished) {
        while (!finished && timestamp::now() < nextping) {
            finished = owner->shutdown.ready();
            if (!finished) sub.wait(nextping); }
        if (finished) continue;
        
        auto token(owner->contactmux.lock());
        nextping = owner->lastcontact + timedelta::seconds(1);
        owner->contactmux.unlock(&token);
        if (timestamp::now() < nextping) continue;
        
        logmsg(loglevel::verbose,
               "sending ping to " + fields::mk(owner->conn.peer()));
        auto snr(owner->conn.allocsequencenr());
        auto pingres(owner->conn.send(
                         wireproto::req_message(proto::PING::tag, snr)));
        if (pingres.isjust()) {
            logmsg(loglevel::failure,
                   "error " + fields::mk(pingres.just()) + " send ping to " +
                   fields::mk(owner->conn.peer()));
            finished = true;
            continue; }
        auto deadline(timestamp::now() + timedelta::seconds(1));
        while (!finished) {
            auto rxres(owner->conn.receive(sub, snr, deadline));
            owner->conn.putsequencenr(snr);
            if (rxres.isfailure()) {
                logmsg(loglevel::failure,
                       "error " + fields::mk(rxres.failure()) +
                       " receiving ping reply from " +
                       fields::mk(owner->conn.peer()));
                finished = true;
                continue; }
            if (rxres.success().issubscription()) {
                finished = owner->shutdown.ready();
                continue; }
            /* Don't actually care about the contents of the message,
               just that the worker can still send them. */
            rxres.success().message()->finish();
            logmsg(loglevel::info,
                   "ping from " + fields::mk(owner->conn.peer()));
            owner->contact();
            break; } }
    owner->owner->killconn(owner->conn); }

void
coordinatorconn::contact() {
    auto token(contactmux.lock());
    lastcontact = timestamp::now();
    contactmux.unlock(&token); }

maybe<error>
coordinatorimpl::statusinterface::message(const wireproto::rx_message &,
                                          controlconn *,
                                          buffer &) {
    return error::unimplemented; }

coordinatorimpl::coordinatorimpl(
    const mastersecret &_ms,
    const registrationsecret &_rs,
    controlserver *cs)
    : ms(_ms),
      newconnlimiter(frequency::hz(100), 100),
      rs(_rs),
      statusiface(),
      controlregistration(cs->service->registeriface(statusiface)) {}

orerror<coordinatorconn *>
coordinatorimpl::startconn(rpcconn &conn) {
    /* Rate limit incoming connections, just because we can. */
    if (!newconnlimiter.probe()) return error::ratelimit;
    auto from(conn.peer());
    /* First message must be HELLO */
    auto hello_r(conn.receive());
    if (hello_r.isfailure()) return hello_r.failure();
    auto msg(hello_r.success());
    if (msg->t != proto::HELLO::tag) return error::unrecognisedmessage;
    auto version(msg->getparam(proto::HELLO::req::version));
    auto nonce(msg->getparam(proto::HELLO::req::nonce));
    auto slavename(msg->getparam(proto::HELLO::req::slavename));
    auto digest(msg->getparam(proto::HELLO::req::digest));
    if (!version || !nonce || !slavename || !digest) {
        return error::missingparameter; }
    logmsg(loglevel::verbose,
           "HELLO version " + fields::mk(version) +
           "nonce " + fields::mk(nonce) +
           "slavename " + fields::mk(slavename) +
           "digest " + fields::mk(digest));
    if (version.just() != 1) {
        return error::badversion; }
    if (!ms.noncevalid(nonce.just(), slavename.just())) {
        logmsg(loglevel::notice,
               "HELLO with invalid nonce from " + fields::mk(from));
        return error::authenticationfailed; }
    if (!slavename.just().samehost(from)) {
        logmsg(loglevel::notice,
               "HELLO with bad host (" + fields::mk(slavename.just()) +
               ", expected host " + fields::mk(from) +")");
        return error::authenticationfailed; }
    if (digest.just() != ::digest("B" +
                                  fields::mk(nonce.just()) +
                                  fields::mk(rs))) {
        logmsg(loglevel::notice,
               "HELLO with invalid digest from " + fields::mk(from));
        return error::authenticationfailed; }
    logmsg(loglevel::notice, "Valid HELLO from " + fields::mk(from));

    /* Send the hello response with a short deadline, because the
       queue should currently be empty and if we have to block at all
       the client must be doing something stupid. */
    auto r(conn.send(wireproto::resp_message(*msg),
                     timestamp::now() + timedelta::seconds(1)));
    msg->finish();
    if (r.isjust()) {
        logmsg(loglevel::failure,
               "failed to send ping reply to " + fields::mk(from));
        return r.just(); }
    
    auto res(new coordinatorconn(conn, this));
    
    /* Hold the lock while we're doing the spawn so that (a) the
       thread can't confuse itself by seeing itself not in the list
       when it starts and (b) no other thread can see the connection
       if the spawn fails. */
    auto token(mux.lock());
    
    auto spawnres(thread::spawn(&res->timerthread_, &res->timerthreadhandle,
                                "timers:" + fields::mk(conn.peer())));
    if (spawnres.isjust()) {
        mux.unlock(&token);
        delete res;
        return spawnres.just(); }
    connections.pushtail(res);
    mux.unlock(&token);
    
    return res; }

void
coordinatorimpl::endconn(coordinatorconn *conn) {
    /* Start shutting down timer thread. */
    assert(!conn->shutdown.ready());
    conn->shutdown.set(true);
    
    /* Remove from the list to prevent any further operations on this
     * connection. */
    {   auto token(mux.lock());
        bool found = false;
        for (auto it(connections.start()); !it.finished(); it.next()) {
            if (conn == *it) {
                it.remove();
                found = true;
                break; } }
        assert(found);
        mux.unlock(&token); }
    
    /* Wait for extant operations to finish. */
    {   subscriber sub;
        subscription ss(sub, conn->idle);
        while (conn->active != 0)
            sub.wait(); }
    
    /* Wait for timer thread to finish shutting down. */
    conn->timerthreadhandle->join();
    
    /* Safe to delete. */
    delete conn; }

void
coordinatorimpl::destroy() {
    stop();
    controlregistration->destroy();
    rpcserver::destroy(); }

orerror<coordinator *>
coordinator::build(
    const mastersecret &ms,
    const registrationsecret &rs,
    const peername &listenon,
    controlserver *cs) {
    auto res(new coordinatorimpl(ms, rs, cs));
    auto r(res->start(listenon, fields::mk("coordinator")));
    if (r.isjust()) {
        delete res;
        return r.just(); }
    return res; }

RPCSERVER(coordinatorconn *)
template class list<coordinatorconn *>;
