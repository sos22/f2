#include "rpcservice.H"
#include "rpcservice.tmpl"

#include <sys/socket.h>
#include <unistd.h>

#include "listenfd.H"
#include "logging.H"
#include "peername.H"
#include "proto.H"
#include "timedelta.H"
#include "version.H"
#include "walltime.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "mutex.tmpl"
#include "thread.tmpl"

#include "fieldfinal.H"

rpcserviceconfig
rpcserviceconfig::dflt() {
    return rpcserviceconfig(
        /* maxoutgoingbytes */
        8192,
        /* maxoutstanding */
        100,
        /* socketrcvsize (Nothing = kernel default) */
        Nothing,
        /* socketsendsize (Nothing = kernel default) */
        Nothing); }
mktupledef(rpcserviceconfig)
mktupledef(rpcserviceconnstatus)
mktupledef(rpcservicestatus)

class rpcservice::rootthread : public thread {
    /* What fd are we listening on?  Accessed outside of this thread
     * by rpcservice::localname() only. */
public:  listenfd const fd;
    /* What service are we exposing? */
private: rpcservice *const owner;
    /* Set once its time to shut down.  All threads (worker and root)
     * must terminate quickly once this is set. */
public:  waitbox<void> shutdown;
    /* All of our workers.  Protected by the worker lock. */
public:  list<worker *> workers;
    /* Leaf lock.  Protects workers. */
public:  mutex_t workerlock;
    /* Count of how many people have connected to this server ever.
     * Only updated by root thread, with no locks. */
public:  unsigned connectionsever;
    /* Count of connections dropped due to errors.  Updated from
     * worker threads with no locks i.e. potentially wrong if we get a
     * lot of errors close together. */
public:  unsigned errorsever;
public:  rootthread(thread::constoken t,
                    listenfd _fd,
                    rpcservice *_owner);
private: void run(clientio);
};

class rpcservice::worker : public thread {
    /* What service are we exposing? */
private: rpcservice *const owner;
    /* What fd are we doing it over?  Initialised to a real fd when we
     * start, downgraded to Nothing when we disconnect (under
     * fdlock). */
public:  maybe<socket_t> fd;
    /* Lock to protect fd.  Acquired from const status() method. */
public:  mutable mutex_t fdlock;
    /* Small leaf lock.  Protects completedcalls. */
public:  mutex_t completionlock;
    /* List of all calls which have been completed, whether by
     * complete() or fail(), but not yet picked up by the thread.
     * Protected by the global attachmentlock.  Publish completedpub
     * when this becomes non-empty. */
public:  list<rpcservice::response *> completedcalls;
    /* Notified after completedcalls becomes non-empty. */
public:  publisher completedpub;
    /* Connection from the root thread's subscriber block to our death
     * publisher. */
private: subscription const ds;
    /* Set once we're far enough through shutdown that we guarantee
     * not to send any more responses to the remote client. */
    /* (We can't use the root thread's shutdown box for this because
     * rpcservice derived classes will sometimes send spurious errors
     * once they see calls being abandoned, so we need to make sure
     * that don't see calls as abandoned until we guarantee not to
     * send more responses to our remote clients). */
public:  waitbox<void> abandoncalls;
    /* How many calls are currently outstanding on this connection?
     * Only updated by worker thread, read (without lock) from status
     * method. */
public:  unsigned currentcalls;
    /* Miscellaneous status fields, with no lock to protect them. */
public:  maybe<walltime> lastactive;
public:  unsigned callsever;
public:  size_t txbytes;
public:  worker(const thread::constoken &t,
                rpcservice *_owner,
                socket_t _fd,
                subscriber &sub);
private: void run(clientio);

public:  typedef rpcserviceconnstatus status_t;
public:  status_t status(mutex_t::token workerlock) const;
};

orerror<listenfd>
rpcservice::open(const peername &listenon) {
    auto s(::socket(listenon.sockaddr()->sa_family,
                    SOCK_STREAM | SOCK_NONBLOCK,
                    0));
    if (s < 0) return error::from_errno();
    if (::bind(s, listenon.sockaddr(), listenon.sockaddrsize()) < 0 ||
        ::listen(s, 10) < 0) {
        ::close(s);
        return error::from_errno(); }
    else return listenfd(s); }

rpcservice::constoken::constoken(const rpcserviceconfig &_config)
    : config(_config) {}

rpcservice::rpcservice(const constoken &token)
    : root(NULL),
      config(token.config) {}

void
rpcservice::startrootthread(listenfd fd) {
    auto t(thread::spawn<rootthread>(
               "R" + fields::mk(fd.localname()),
               fd,
               this));
    root = t.unwrap();
    t.go(); }

peername
rpcservice::localname() const {
    /* fd remains valid as long as root does, which is long enough
     * that the caller wouldn't have invoked us if accessing fd were
     * dangerous. */
    return root->fd.localname(); }

rpcservice::rootthread::rootthread(thread::constoken t,
                                   listenfd _fd,
                                   rpcservice *_owner)
    : thread(t),
      fd(_fd),
      owner(_owner),
      shutdown(),
      workers(),
      workerlock(),
      connectionsever(0),
      errorsever(0) {}

void
rpcservice::rootthread::run(clientio io) {
    subscriber sub;
    subscription shutdownsub(sub, shutdown.pub);
    iosubscription incomingsub(sub, fd.poll());
    while (!shutdown.ready()) {
        auto s(sub.wait(io));
        if (s == &shutdownsub) continue;
        else if (s == &incomingsub) {
            connectionsever++;
            auto l(fd.accept());
            if (l.isfailure()) {
                l.failure().warn("accepting on " + fields::mk(fd.localname()));
                /* Wait around for a bit before rearming so that a
                 * persistent failure doesn't completely spam the
                 * logs. */
                s = sub.wait(io, timestamp::now() + timedelta::seconds(1));
                assert(s == NULL || s == &shutdownsub);
                incomingsub.rearm();
                errorsever++;
                continue; }
            incomingsub.rearm();
            auto r(thread::spawn<worker>(
                       "S" + fields::mk(fd.localname()),
                       owner,
                       l.success(),
                       sub));
            workerlock.locked([this, &r] (mutex_t::token) {
                    workers.pushtail(r.go()); } ); }
        else {
            /* Must have been a thread death subscription. */
            auto died(static_cast<worker *>(s->data));
            auto death(died->hasdied());
            if (death == Nothing) continue;
            workerlock.locked([this, died] (mutex_t::token) {
                    for (auto it(workers.start()); true; it.next()){
                        if (*it == died) {
                            it.remove();
                            break; } } } );
            died->join(death.just()); } }
    /* Time to shut down.  Stop accepting new connections and wait for
     * the existing ones to finish.  They should already be shutting
     * down because they watch the same shutdown box as us. */
    /* We only get here once shutdown is set, and shutdown is only set
     * from destroy(), so if localname() is still using the FD then the
     * caller must be racing localname() and destroy(), which is already
     * a bug, so we know that closing fd here is safe. */
    fd.close();
    while (true) {
        auto w(workerlock.locked<worker *>([this] (mutex_t::token) -> worker *{
                    if (workers.empty()) return NULL;
                    else return workers.pophead(); }));
        if (w == NULL) break;
        else w->join(io); } }

rpcservice::worker::worker(const thread::constoken &t,
                           rpcservice *_owner,
                           socket_t _fd,
                           subscriber &sub)
    : thread(t),
      owner(_owner),
      fd(_fd),
      fdlock(),
      completedcalls(),
      completedpub(),
      ds(sub, pub(), this),
      abandoncalls(),
      currentcalls(0),
      lastactive(Nothing),
      callsever(0),
      txbytes(0) {}

void
rpcservice::worker::run(clientio io) {
    auto fdd(fd.just());
    subscriber sub;
    subscription shutdownsub(sub, owner->root->shutdown.pub);
    subscription completedcall(sub, completedpub);
    maybe<iosubscription> insub(Nothing);
    insub.mkjust(sub, fdd.poll(POLLIN));
    maybe<iosubscription> outsub(Nothing);
    /* Things to send which have not yet made it into outbuf. */
    list<response *> responses;
    /* Things which the derived class has not yet finished generating
     * a response to. */
    list<response *> incompletecalls;
    buffer outbuf;
    buffer inbuf;
    /* Has the client sent a valid HELLO yet? */
    bool helloed = false;
    peername remotename(peername::all(peername::port::any));
    {   auto remotename_(fdd.peer());
        if (remotename_.isfailure()) goto conndead;
        remotename = remotename_.success(); }
    if (owner->config.socketsendsize != Nothing) {
        int buf(owner->config.socketsendsize.just());
        if (::setsockopt(fdd.fd, SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf)) < 0){
#ifndef COVERAGESKIP
            error::from_errno().warn("setting send buffer for " +
                                     fields::mk(remotename));
            owner->root->errorsever++;
            goto conndead;
#endif
        } }
    if (owner->config.socketrcvsize != Nothing) {
        int buf(owner->config.socketrcvsize.just());
        if (::setsockopt(fdd.fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf)) < 0){
#ifndef COVERAGESKIP
            error::from_errno().warn("setting receive buffer for " +
                                     fields::mk(remotename));
            owner->root->errorsever++;
            goto conndead;
#endif
        } }
    while (!owner->root->shutdown.ready()) {
        auto s(sub.wait(io));
        if (s == &shutdownsub) continue;
        else if (s == &completedcall) {
            list<response *> incoming;
            completionlock.locked([&incoming, this] (mutex_t::token) {
                    incoming.transfer(completedcalls); });
            /* XXX O(n) lookup is not efficient.  It's not usually too
             * bad, because (a) n is usually small, (b) the thing
             * we're looking for is usually near the start of the
             * list, and (c) we're not under any locks, but it could
             * still benefit from a bit of optimisation. */
            for (auto it(incoming.start()); !it.finished(); it.next()) {
                for (auto it2(incompletecalls.start()); true; it2.next()) {
                    if (*it2 == *it) {
                        it2.remove();
                        break; } } }
            responses.transfer(incoming);
            if (!responses.empty() && outsub == Nothing) {
                outsub.mkjust(sub, fdd.poll(POLLOUT)); } }
        else if (outsub != Nothing && s == &outsub.just()) {
            /* outsub is only armed when there's stuff in the
             * responses list. */
            assert(!responses.empty() || !outbuf.empty());
            while (outbuf.avail() < owner->config.maxoutgoingbytes &&
                   !responses.empty()) {
                auto r(responses.pophead());
                r->inner.serialise(outbuf);
                /* It won't reach the responses queue until the
                 * service implementation is done with it, so this is
                 * a good place to delete it. */
                delete r;
                /* Things in the outgoing buffer count as complete for
                 * the purposes of applying backpressure to the
                 * client. */
                currentcalls--; }
            assert(!outbuf.empty());
            auto r(outbuf.sendfast(fdd));
            txbytes = outbuf.avail();
            if (r.isfailure() && r != error::wouldblock) {
                r.failure().warn("sending to " + fields::mk(remotename));
                /* Give up after the first error.  Most errors are
                 * persistent, anyway. */
                owner->root->errorsever++;
                break; }
            if (responses.empty() && outbuf.empty()) outsub = Nothing;
            else outsub.just().rearm();
            if (currentcalls < owner->config.maxoutstanding &&
                insub == Nothing) {
                insub.mkjust(sub, fdd.poll(POLLIN)); } }
        else if (insub != Nothing && s == &insub.just()) {
            auto r(inbuf.receivefast(fdd));
            if (r.isfailure()) {
                r.failure().warn("receiving from " + fields::mk(remotename));
                owner->root->errorsever++;
                break; }
            while (true) {
                auto rr(wireproto::rx_message::fetch(inbuf));
                if (rr == error::underflowed) break;
                if (rr.isfailure()) {
                    rr.failure().warn(
                        "parsing message from " + fields::mk(remotename));
                    owner->root->errorsever++;
                    goto conndead; }
                lastactive = ::walltime::now();
                auto resp(new response(rr.success(), this));
                incompletecalls.pushtail(resp);
                callsever++;
                currentcalls++;
                /* XXX we buffer up responses in outbuf before sending them.
                 * It's not clear that that's worthwhile:
                 *
                 * (a) Usually, there's only one response in the batch, so we
                 *     don't actually gain anything.
                 * (b) It potentially makes latency worse, and I'm willing to
                 *     guess that we're more sensitive to latency than to
                 *     throughput.
                 * (c) It introduces dependencies between seemingly-unrelated
                 *     calls which happen to get bundled together.  Not passing
                 *     call() a clientio token makes that less bad, because
                 *     it shouldn't be waiting for complicated things, but
                 *     it's still there.
                 * (d) It makes things more complicated than they need to be.
                 *
                 * Consider moving to a model which more strongly
                 * prioritises sending replies over receiving more
                 * requests. */
                /* PING is trivial */
                if (rr.success().tag() == proto::PING::tag) resp->complete();
                /* Special handling for HELLO */
                else if (rr.success().tag() == proto::HELLO::tag) {
                    if (helloed) resp->fail(error::toolate);
                    else if (rr.success().getparam(proto::HELLO::req::version)
                             != version::current) {
                        resp->fail(error::badversion); }
                    else {
                        resp->complete();
                        helloed = true; } }
                /* Normal messages are invalid before we receive a
                 * valid HELLO */
                else if (!helloed) resp->fail(error::toosoon);
                /* Normal message, normal processing. */
                else owner->call(rr.success(), resp); }
            if (currentcalls >= owner->config.maxoutstanding) insub = Nothing;
            else insub.just().rearm(); } }
  conndead:
    fdlock.locked([this] (mutex_t::token) {
            fd.just().close();
            fd = Nothing; });
    /* We can't accept any more calls.  Abandon any outstanding ones
     * and wait for them to complete. */
    abandoncalls.set(); /* Causes all of our outstanding calls to
                         * start failng quickly. */
    insub = Nothing;
    outsub = Nothing;
    completedpub.publish();
    while (!incompletecalls.empty()) {
        auto s(sub.wait(io));
        if (s == &shutdownsub) continue;
        assert(s == &completedcall);
        list<response *> incoming;
        completionlock.locked([&incoming, this] (mutex_t::token) {
                incoming.transfer(completedcalls); });
        for (auto it(incoming.start()); !it.finished(); it.next()) {
            for (auto it2(incompletecalls.start()); true; it2.next()) {
                if (*it2 == *it) {
                    it2.remove();
                    break; } } }
        responses.transfer(incoming); }
    assert(completedcalls.empty());
    /* Discard anything which was already complete and which we
     * haven't gotten around to sending yet. */
    while (!responses.empty()) delete responses.pophead(); }

rpcservice::worker::status_t
rpcservice::worker::status(mutex_t::token /* workerlock */) const {
    status_t res(Nothing,
                 lastactive,
                 callsever,
                 Nothing,
                 currentcalls,
                 txbytes);
    fdlock.locked([this, &res] (mutex_t::token) {
            if (fd.isjust()) {
                auto r(fd.just().peer());
                if (r.issuccess()) res.remote = r.success();
                res.fd = fd.just().status(); } } );
    return res; }

rpcservice::response::response(const wireproto::rx_message &rxm,
                               worker *_owner)
    : inner(rxm),
      owner(_owner) {}

const waitbox<void> &
rpcservice::response::abandoned() const {
    return owner->abandoncalls; }

void
rpcservice::response::complete() {
    /* Note that once we're in the completed calls list and we've
     * dropped the completion lock the owner is at liberty to delete
     * us at any time. */
    owner->completionlock.locked([this] (mutex_t::token) {
            auto notify(owner->completedcalls.empty());
            owner->completedcalls.pushtail(this);
            if (notify) owner->completedpub.publish(); }); }

void
rpcservice::response::fail(error err) {
    inner.flush();
    inner.addparam(wireproto::err_parameter, err);
    complete(); }

rpcservice::response::~response() {}

/* Note that this can return fd after it's been closed by the service
 * worker.  The caller is expected to make sure that doesn't happen.
 * In practice, this API is only sensible to use from the test
 * harness. */
socket_t
rpcservice::response::__connection__() const { return owner->fd.just(); }

rpcservice::status_t
rpcservice::status() const {
    list< ::rpcserviceconnstatus> conns;
    root->workerlock.locked([this, &conns] (mutex_t::token t) {
            for (auto it(root->workers.start()); !it.finished(); it.next()) {
                conns.pushtail((*it)->status(t)); } });
    return status_t(conns,
                    root->fd.status(),
                    root->fd.localname(),
                    root->connectionsever,
                    root->errorsever); }

void
rpcservice::destroy(clientio io) {
    root->shutdown.set();
    /* Waits for any outstanding calls to complete, so it's safe to
     * call the derived class destructor once this finishes.. */
    root->join(io);
    root = NULL;
    destroying(io);
    delete this; }

void
rpcservice::destroying(clientio) {}

rpcservice::~rpcservice() {
    assert(root == NULL); }
