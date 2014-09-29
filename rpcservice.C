#include "rpcservice.H"
#include "rpcservice.tmpl"

#include <sys/socket.h>
#include <unistd.h>

#include "listenfd.H"
#include "peername.H"
#include "proto.H"
#include "timedelta.H"
#include "version.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "thread.tmpl"

const rpcserviceconfig
rpcserviceconfig::dflt(
    /* maxoutgoingbytes */
    8192,
    /* maxoutstanding */
    100);

mktupledef(rpcserviceconfig)

class rpcservice::rootthread : public thread {
    /* What fd are we listening on?  Accessed outside of this thread
     * by rpcservice::localname() only. */
public:  listenfd const fd;
    /* What service are we exposing? */
private: rpcservice *const owner;
    /* Set once its time to shut down.  All threads (worker and root)
     * must terminate quickly once this is set. */
public:  waitbox<void> shutdown;
public:  rootthread(thread::constoken t,
                    listenfd _fd,
                    rpcservice *_owner);
private: void run(clientio);
};

class rpcservice::worker : public thread {
    /* What service are we exposing? */
private: rpcservice *const owner;
    /* What fd are we doing it over? */
private: socket_t const fd;
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
public:  worker(const thread::constoken &t,
                rpcservice *_owner,
                socket_t _fd,
                subscriber &sub);
private: void run(clientio);
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
      shutdown() {}

void
rpcservice::rootthread::run(clientio io) {
    subscriber sub;
    subscription shutdownsub(sub, shutdown.pub);
    iosubscription incomingsub(sub, fd.poll());
    list<worker *> workers;
    while (!shutdown.ready()) {
        auto s(sub.wait(io));
        if (s == &shutdownsub) continue;
        else if (s == &incomingsub) {
            auto l(fd.accept());
            if (l.isfailure()) {
                l.failure().warn("accepting on " + fields::mk(fd.localname()));
                /* Wait around for a bit before rearming so that a
                 * persistent failure doesn't completely spam the
                 * logs. */
                s = sub.wait(io, timestamp::now() + timedelta::seconds(1));
                assert(s == NULL || s == &shutdownsub);
                incomingsub.rearm();
                continue; }
            incomingsub.rearm();
            auto r(thread::spawn<worker>(
                       "S" + fields::mk(fd.localname()),
                       owner,
                       l.success(),
                       sub));
            workers.pushtail(r.go()); }
        else {
            /* Must have been a thread death subscription. */
            auto died(static_cast<worker *>(s->data));
            auto death(died->hasdied());
            if (death == Nothing) continue;
            for (auto it(workers.start()); true; it.next()){
                if (*it == died) {
                    it.remove();
                    break; } }
            died->join(death.just()); } }
    /* Time to shut down.  Stop accepting new connections and wait for
     * the existing ones to finish.  They should already be shutting
     * down because they watch the same shutdown box as us. */
    /* We only get here once shutdown is set, and shutdown is only set
     * from destroy(), so if localname() is still using the FD then the
     * caller must be racing localname() and destroy(), which is already
     * a bug, so we know that closing fd here is safe. */
    fd.close();
    while (!workers.empty()) {
        auto w(workers.pophead());
        w->join(io); } }

rpcservice::worker::worker(const thread::constoken &t,
                           rpcservice *_owner,
                           socket_t _fd,
                           subscriber &sub)
    : thread(t),
      owner(_owner),
      fd(_fd),
      completedcalls(),
      completedpub(),
      ds(sub, pub(), this) {}

void
rpcservice::worker::run(clientio io) {
    subscriber sub;
    subscription shutdownsub(sub, owner->root->shutdown.pub);
    subscription completedcall(sub, completedpub);
    maybe<iosubscription> insub(Nothing);
    insub.mkjust(sub, fd.poll(POLLIN));
    maybe<iosubscription> outsub(Nothing);
    /* Things to send which have not yet made it into outbuf. */
    list<response *> responses;
    /* Things which the derived class has not yet finished generating
     * a response to. */
    list<response *> incompletecalls;
    buffer outbuf;
    buffer inbuf;
    unsigned outstanding = 0;
    /* Has the client sent a valid HELLO yet? */
    bool helloed = false;
    peername remotename(peername::all(peername::port::any));
    {   auto remotename_(fd.peer());
        if (remotename_.isfailure()) goto conndead;
        remotename = remotename_.success(); }
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
                outsub.mkjust(sub, fd.poll(POLLOUT)); } }
        else if (outsub != Nothing && s == &outsub.just()) {
            /* outsub is only armed when there's stuff in the
             * responses list. */
            assert(!responses.empty());
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
                outstanding--; }
            assert(!outbuf.empty());
            auto r(outbuf.sendfast(fd));
            if (r.isfailure() && r != error::wouldblock) {
                r.failure().warn("sending to " + fields::mk(remotename));
                /* Give up after the first error.  Most errors are
                 * persistent, anyway. */
                break; }
            if (responses.empty() && outbuf.empty()) outsub = Nothing;
            else outsub.just().rearm();
            if (outstanding < owner->config.maxoutstanding &&
                insub == Nothing) {
                insub.mkjust(sub, fd.poll(POLLIN)); } }
        else if (insub != Nothing && s == &insub.just()) {
            auto r(inbuf.receivefast(fd));
            if (r.isfailure()) {
                auto ss(fd.peer());
                r.failure().warn("receiving from " + fields::mk(remotename));
                break; }
            while (true) {
                auto rr(wireproto::rx_message::fetch(inbuf));
                if (rr == error::underflowed) break;
                if (rr.isfailure()) {
                    r.failure().warn(
                        "parsing message from " + fields::mk(remotename));
                    goto conndead; }
                auto resp(new response(rr.success(), this));
                incompletecalls.pushtail(resp);
                outstanding++;
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
            if (outstanding >= owner->config.maxoutstanding) insub = Nothing;
            else insub.just().rearm(); } }
  conndead:
    fd.close();
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
