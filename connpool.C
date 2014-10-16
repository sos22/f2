#include "connpool.H"

#include "beaconclient.H"
#include "fields.H"
#include "logging.H"
#include "proto.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "mutex.tmpl"
#include "rpcservice.tmpl"
#include "thread.tmpl"

mktupledef(connpoolconfig)

connpoolconfig
connpoolconfig::dflt() {
    return connpoolconfig(rpcclientconfig::dflt(),
                          /* connecttimeout */
                          timedelta::seconds(5),
                          /* callretries */
                          10,
                          /* expirytime */
                          timedelta::seconds(300),
                          /* dupecalls */
                          probability::never); }

pooledconnection::pooledconnection(connpool *_owner,
                                   const slavename &_name)
    : mux(),
      name(_name),
      owner(_owner),
      refcount(1),
      idledat(Nothing),
      outstanding(),
      inner(Nothing),
      connectsub(Nothing),
      connectstart(Nothing),
      errored(false),
      newcall(),
      newcallsub(Nothing) {}
void
pooledconnection::disconnect(
    mutex_t::token /* conn lock */) {
    bool aborted;
    refcount++;
    /* Rewind any still-outstanding calls on the old connection. */
    for (auto it2(outstanding.start());
         !it2.finished();
         aborted ? it2.remove()
                 : it2.next()) {
        auto Call(*it2);
        aborted = Call->mux.locked<bool>([this, Call] (mutex_t::token) {
                Call->sub = Nothing;
                if (Call->inner) Call->inner->abort();
                else Call->nrretries++;
                Call->inner = NULL;
                return Call->aborted; });
        if (aborted) {
            Call->owner = NULL;
            delete Call;
            put(); } }
    /* Tear down however much of the old connection we have. */
    connectsub = Nothing;
    if (inner.isjust()) {
        if (inner.just().isleft()) inner.just().left()->abort();
        else delete inner.just().right(); }
    inner = Nothing;
    put(); }

orerror<wireproto::rx_message *>
pooledconnection::call(clientio io,
                       const wireproto::req_message &msg,
                       maybe<timestamp> deadline) {
    return call(msg)->pop(io, deadline); }

pooledconnection::asynccall::asynccall(pooledconnection *_owner,
                                       const wireproto::req_message &_msg)
    : mux(),
      owner(_owner),
      msg(_msg.clone()),
      nrretries(0),
      inner(NULL),
      res(Nothing),
      sub(Nothing),
      _pub(),
      aborted(false) {}

pooledconnection::asynccall::asynccall()
    : mux(),
      owner(NULL),
      msg(NULL),
      nrretries(0),
      inner(NULL),
      res(error::disconnected),
      sub(Nothing),
      _pub(),
      aborted(false) {}

pooledconnection::asynccall::token::token() {}

maybe<pooledconnection::asynccall::token>
pooledconnection::asynccall::finished() const {
    return mux.locked<maybe<pooledconnection::asynccall::token> >(
        [this]
        (mutex_t::token) -> maybe<pooledconnection::asynccall::token> {
            assert(!aborted);
            if (res.isjust()) return token();
            else return Nothing; } ); }

const publisher &
pooledconnection::asynccall::pub() const { return _pub; }

orerror<wireproto::rx_message *>
pooledconnection::asynccall::pop(token) {
    assert(!aborted);
    /* Because we have a token, and they're only constructed once we
     * have a result. */
    assert(res.isjust());
    /* Because maintenance thread is supposed to make sure of these
     * before setting res. */
    assert(inner == NULL);
    assert(owner == NULL);
    assert(sub == Nothing);
    /* No lock because res never changes once it's set. */
    auto result(res.just());
    res = Nothing;
    delete this;
    return result; }

void
pooledconnection::asynccall::abort() {
    auto detached = mux.locked<bool>([this] (mutex_t::token) {
            assert(!aborted);
            aborted = true;
            /* Could let the maintenance thread do this, but doing it
             * early makes it more likely that we'll stop it before we
             * send it to the other side. */
            if (inner) {
                sub = Nothing;
                inner->abort();
                inner = NULL; }
            if (owner) owner->newcall.publish();
            return owner == NULL; });
    if (detached) delete this; }

orerror<wireproto::rx_message *>
pooledconnection::asynccall::pop(clientio io, maybe<timestamp> deadline) {
    maybe<token> t(Nothing);
    {   subscriber subscribe;
        subscription ss(subscribe, pub());
        t = finished();
        while (t == Nothing) {
            if (subscribe.wait(io, deadline) == NULL) goto timeout;
            t = finished(); } }
    return pop(t.just());
 timeout:
    abort();
    return error::timeout; }

pooledconnection::asynccall::~asynccall() {
    assert(owner == NULL);
    assert(sub == Nothing);
    assert(inner == NULL);
    if (res != Nothing && res.just().issuccess()) delete res.just().success();
    delete msg; }

pooledconnection::asynccall *
pooledconnection::call(const wireproto::req_message &msg) {
    return mux.locked<asynccall *>(
        [this, &msg]
        (mutex_t::token) {
            if (owner) {
                refcount++;
                if (refcount == 1) idledat = Nothing;
                auto res(new asynccall(this, msg));
                outstanding.pushtail(res);
                newcall.publish();
                return res; }
            else return new asynccall(); }); }

void
pooledconnection::put() {
    bool die = mux.locked<bool>([this] (mutex_t::token) {
            assert(refcount > 0);
            assert(idledat == Nothing);
            refcount--;
            if (refcount == 0) {
                idledat = timestamp::now();
                if (owner == NULL) return true;
                owner->connchanged.publish(); }
            return false; });
    if (die) delete this; }

pooledconnection::~pooledconnection() {
    assert(owner == NULL);
    assert(refcount == 0);
    assert(idledat != Nothing);
    assert(newcallsub == Nothing);
    /* Don't need a full token because we're a destructor, so it'd
     * better be single-threaded by now. */
    disconnect(mux.DUMMY()); }

connpool::controliface::controliface(controlserver *cs,
                                     connpool *_owner)
    : controlinterface(cs),
      owner(_owner) { start(); }

void
connpool::controliface::getstatus() const {
    logmsg(loglevel::info, fields::mk(owner->status())); }

connpool::connpool(beaconclient *_bc,
                   controlserver *_cs,
                   const connpoolconfig &_config)
    : mux(),
      connchanged(),
      config(_config),
      shutdown(),
      bc(_bc),
      connections(),
      maintain(thread::start<connpoolmaintenance>(
                   fields::mk("connpool"),
                   this)),
      control(Nothing) { if (_cs != NULL) control.mkjust(_cs, this); }

mktupledef(connpoolstatus);

class connpoolmaintenance : public thread {
private: connpool *const owner;
public:  connpoolmaintenance(const constoken &t, connpool *owner);
public:  void run(clientio); };

connpoolmaintenance::connpoolmaintenance(const constoken &t,
                                         connpool *_owner)
    : thread(t),
      owner(_owner) {}

void
connpoolmaintenance::run(clientio io) {
    subscriber sub;
    subscription ss(sub, owner->shutdown.pub);
    subscription conns(sub, owner->connchanged);
    subscription beacon(sub, owner->bc->changed());
    /* Check the conn list and beacon list as soon as we start. */
    conns.set();
    beacon.set();
    while (!owner->shutdown.ready()) {
        /* Figure out when the next timeout is. */
        maybe<timestamp> wakeat(Nothing);
        owner->mux.locked([this, &wakeat] (mutex_t::token) {
                for (auto it(owner->connections.start());
                     !it.finished();
                     it.next()) {
                    auto e(*it);
                    e->mux.locked([this, e, &wakeat] (mutex_t::token) {
                            if (e->refcount != 0) return;
                            assert(e->idledat.isjust());
                            if (wakeat == Nothing ||
                                e->idledat.just() + owner->config.expirytime <
                                    wakeat.just()) {
                                wakeat = e->idledat.just() +
                                         owner->config.expirytime; } }); } });
        /* Wait for something to happen. */
        auto notified(sub.wait(io));
        if (notified == &ss) continue;
        list<pooledconnection *> doomed;
        list<pooledconnection *> needbeaconpoll;
        list<pooledconnection *> needconnectpoll;
        list<pooledconnection *> needcall;
        list<pooledconnection::asynccall *> needcompletepoll;
        if (((uintptr_t)notified->data & 3) == 1) {
            needconnectpoll.pushtail(
                (pooledconnection *)((uintptr_t)notified->data & ~3ul)); }
        if (((uintptr_t)notified->data & 3) == 2) {
            needcompletepoll.pushtail(
                (pooledconnection::asynccall *)
                ((uintptr_t)notified->data & ~3ul)); }
        if (((uintptr_t)notified->data & 3) == 3) {
            needcall.pushtail(
                (pooledconnection *)((uintptr_t)notified->data & ~3ul)); }
        /* Check for errors, idle timeouts, and things which need us
         * to start a connect. */
        owner->mux.locked(
            [this, &beacon, &needbeaconpoll, &needcall, notified, &sub]
            (mutex_t::token) {
                bool remove;
                for (auto it(owner->connections.start());
                     !it.finished();
                     remove ? it.remove()
                            : it.next()) {
                    auto conn(*it);
                    remove = conn->mux.locked<bool>(
                        [this, &beacon, conn, &needbeaconpoll,
                         &needcall, notified, &sub]
                        (mutex_t::token connlock) {
                            if (conn->newcallsub == Nothing) {
                                conn->newcallsub.mkjust(sub, conn->newcall);
                                needcall.pushtail(conn); }
                            if (conn->connectstart.isjust() &&
                                timestamp::now() - conn->connectstart.just() >
                                    owner->config.connecttimeout) {
                                /* Took too long to connect -> run
                                 * error recovery machine. */
                                conn->errored = true; }
                            if (conn->errored) {
                                /* This connection has an error.
                                 * Disconnect it. */
                                conn->errored = false;
                                conn->disconnect(connlock);
                                if (conn->refcount == 0) {
                                    /* No more work -> throw it
                                     * away. */
                                    assert(conn->outstanding.empty());
                                    return true; }
                                else {
                                    /* Still have some work ->
                                     * reconnect. */
                                    needbeaconpoll.pushtail(conn);
                                    return false; } }
                            else if (conn->refcount == 0 &&
                                     timestamp::now() - conn->idledat.just() >
                                         owner->config.expirytime) {
                                /* This connection has been idle for a
                                 * while -> disconnect. */
                                assert(conn->outstanding.empty());
                                conn->disconnect(connlock);
                                return true; }
                            else if (conn->inner == Nothing &&
                                     notified == &beacon) {
                                /* Check the beacon for a posible
                                 * peername for this conn. */
                                needbeaconpoll.pushtail(conn);
                                return false; }
                            else return false; } );
                    if (remove) {
                        conn->newcallsub = Nothing;
                        conn->owner = NULL;
                        delete conn; } } } );
        /* Check if anything which needed beacon results has made
         * progress. */
        while (!needbeaconpoll.empty()) {
            auto conn(needbeaconpoll.pophead());
            if (conn->connectstart == Nothing) {
                conn->connectstart = timestamp::now(); }
            auto p(owner->bc->poll(conn->name));
            if (p == Nothing) continue;
            /* Got a peername -> start the connect() proper. */
            auto inner(rpcclient::connect(p.just().name));
            conn->mux.locked(
                [conn, inner, &needconnectpoll, &sub]
                (mutex_t::token) {
                    assert(conn->inner == Nothing);
                    conn->inner = either<rpcclient::asyncconnect *, rpcclient *>
                        ::left(inner);
                    conn->connectsub.mkjust(sub,
                                            inner->pub(),
                                            (void *)((uintptr_t)conn | 1));
                    needconnectpoll.pushtail(conn); }); }
        /* Check if anything which was waiting for the rpcclient to
         * connect has made progress. */
        while (!needconnectpoll.empty()) {
            auto conn(needconnectpoll.pophead());
            conn->mux.locked(
                [this, conn, &conns, &needcall]
                (mutex_t::token) {
                    assert(conn->inner.isjust());
                    assert(conn->inner.just().isleft());
                    auto tok(conn->inner.just().left()->finished());
                    if (tok == Nothing) return;
                    /* rpcclient connect() finished -> pick up the
                     * results. */
                    conn->connectsub = Nothing;
                    auto inner(conn->inner.just().left()->pop(tok.just()));
                    if (inner.isfailure()) {
                        /* connect failed -> run the error handling
                         * machine next time around. */
                        conn->errored = true;
                        conn->inner = Nothing;
                        owner->connchanged.publish(); }
                    else {
                        /* connect succeeded. */
                        conn->connectstart = Nothing;
                        conn->inner =
                            either<rpcclient::asyncconnect *, rpcclient *>
                            ::right(inner.success());
                        needcall.pushtail(conn); } } ); }
        /* Check for connections with new calls and for abort()ed
         * calls. */
        while (!needcall.empty()) {
            auto conn(needcall.pophead());
            conn->mux.locked(
                [this, conn, &needcompletepoll, &sub]
                (mutex_t::token) {
                    if (conn->inner == Nothing ||
                        conn->inner.just().isleft()) {
                        return; }
                    /* Retry any outstanding calls. */
                    bool remove;
                    for (auto it(conn->outstanding.start());
                         !it.finished();
                         remove ? it.next() : it.remove()) {
                        auto call(*it);
                        assert(call->owner == conn);
                        assert(call->res == Nothing);
                        if (call->mux.locked<bool>([call] (mutex_t::token) {
                                    return call->aborted; })) {
                            remove = true; }
                        else if (call->inner != NULL) continue;
                        else if (call->nrretries++ < owner->config.callretries){
                            auto innerconn(conn->inner.just().right());
                            call->inner = innerconn->call(*call->msg);
                            if (owner->config.dupecalls.random()) {
                                /* XXX stupid stupid hack: give it a
                                 * few milliseconds to actually send
                                 * the call before we send it again,
                                 * even though that means sleeping
                                 * while holding important locks and
                                 * without a client IO token.  Just
                                 * barely good enough for a debug-only
                                 * option. */
                                {   subscriber debugsub;
                                    subscription sillysub(debugsub,
                                                          call->inner->pub());
                                    auto deadline(timestamp::now() +
                                                  timedelta::milliseconds(50));
                                    while (call->inner->finished() == Nothing &&
                                           debugsub.wait(clientio::CLIENTIO,
                                                         deadline) != NULL)
                                        ; }
                                call->inner->abort();
                                call->inner = innerconn->call(*call->msg); }
                            call->sub.mkjust(sub,
                                             call->inner->pub(),
                                             (void *)((uintptr_t)call | 2));
                            needcompletepoll.pushtail(call); }
                        else {
                            call->mux.locked([call] (mutex_t::token) {
                                    call->res = error::disconnected;
                                    call->_pub.publish();
                                    call->owner = NULL; });
                            remove = true; }
                        if (remove) {
                            call->sub = Nothing;
                            if (call->inner) call->inner->abort();
                            call->owner = NULL;
                            delete call; } } } ); }
        /* Check if anything which was waiting for a response has made
         * progress. */
        while (!needcompletepoll.empty()) {
            auto call(needcompletepoll.pophead());
            auto conn(call->owner);
            assert(call->inner != NULL);
            assert(conn != NULL);
            auto tok(call->inner->finished());
            if (tok == Nothing) continue;
            /* Call completed. */
            call->sub = Nothing;
            auto res(call->inner->pop(tok.just()));
            call->inner = NULL;
            if (res == error::disconnected || res == error::invalidmessage) {
                /* These errors cause the connection to be torn down
                 * so that we retry the calls. */
                conn->mux.locked([this, conn] (mutex_t::token) {
                        conn->errored = true;
                        owner->connchanged.publish(); }); }
            else {
                /* Otherwise, pass the result back to whoever
                 * initiated the call; the maintenance thread will
                 * have nothing more to do with it. */
                conn->mux.locked([call, conn] (mutex_t::token) {
                        for (auto it(conn->outstanding.start());
                             true;
                             it.next()) {
                            if (*it == call) {
                                it.remove();
                                break; } } });
                auto destroy = call->mux.locked<bool>(
                    [call, res]
                    (mutex_t::token) {
                        assert(call->res == Nothing);
                        call->res = res;
                        call->owner = NULL;
                        call->_pub.publish();
                        return call->aborted; });
                if (destroy) delete call;
                conn->put(); } } }
    /* We're about to die.  Finish off any work we still have
     * outstanding first. */
    owner->mux.locked(
        [this]
        (mutex_t::token) {
            while (!owner->connections.empty()) {
                auto conn(owner->connections.pophead());
                bool die = conn->mux.locked<bool>(
                    [this, conn]
                    (mutex_t::token) {
                        while (!conn->outstanding.empty()) {
                            auto call(conn->outstanding.pophead());
                            bool d = call->mux.locked<bool>(
                                [call, conn]
                                (mutex_t::token) {
                                    assert(call->owner == conn);
                                    call->owner = NULL;
                                    call->res = error::disconnected;
                                    call->_pub.publish();
                                    return call->aborted; });
                            if (d) delete call; }
                        assert(conn->owner == owner);
                        conn->newcallsub = Nothing;
                        conn->owner = NULL;
                        return conn->refcount == 0; });
                if (die) delete conn; } }); }

pooledconnection *
connpool::connect(const slavename &name) {
    return mux.locked<pooledconnection *>(
        [this, &name]
        (mutex_t::token) {
            for (auto it(connections.start());
                 !it.finished();
                 it.next()) {
                auto conn(*it);
                if (conn->name == name) {
                    conn->refcount++;
                    return conn; } }
            auto res(new pooledconnection(this, name));
            connections.pushtail(res);
            connchanged.publish();
            return res; }); }

connpool::status_t
connpool::status() const { return status_t(config); }

connpool::~connpool() {
    shutdown.set();
    /* Maintenance thread is guaranteed to stop quickly once shutdown
     * is set, so this doesn't need a clientio token. */
    maintain->join(clientio::CLIENTIO);
    assert(connections.empty()); }
