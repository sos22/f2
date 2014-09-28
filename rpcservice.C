#include "rpcservice.H"
#include "rpcservice.tmpl"

#include <sys/socket.h>
#include <unistd.h>

#include "listenfd.H"
#include "peername.H"
#include "proto.H"
#include "timedelta.H"
#include "version.H"

#include "thread.tmpl"

/* Global leaf lock to protect the mapping between outstanding calls
 * and worker threads. */
static mutex_t
attachmentlock;

mktupledef(rpcserviceconfig)

class rpcservice::rootthread : public thread {
    /* What fd are we listening on? */
private: listenfd const fd;
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
    /* Connection from the root thread's subscriber block to our death
     * publisher. */
private: subscription const ds;
    /* List of all calls which have been completed, whether by
     * complete() or fail().  Protected by the global attachmentlock.
     * Publish completedpub when this becomes non-empty. */
public:  list<rpcservice::response *> completedcalls;
    /* Notified when completedcalls becomes non-empty. */
public:  publisher completedpub;
public:  worker(const thread::constoken &t,
                rpcservice *_owner,
                socket_t _fd,
                subscriber &sub);
private: void run(clientio);
};

orerror<int>
rpcservice::open(const peername &listenon) {
    auto s(::socket(listenon.sockaddr()->sa_family,
                    SOCK_STREAM | SOCK_NONBLOCK,
                    0));
    if (s < 0) return error::from_errno();
    if (::bind(s, listenon.sockaddr(), listenon.sockaddrsize()) < 0 ||
        ::listen(s, 10) < 0) {
        ::close(s);
        return error::from_errno(); }
    else return s; }

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
            bool found = false;
            for (auto it(workers.start()); !found && !it.finished(); it.next()){
                found = *it == died; }
            assert(found == true);
            died->join(death.just()); } }
    /* Time to shut down.  Stop accepting new connections and wait for
     * the existing ones to finish.  They should already be shutting
     * down because they watch the same shutdown box as us. */
    fd.close();
    /* Workers guarantee to shut down quickly once shutdown is set, so
     * this doesn't need a full clientio token. */
    while (!workers.empty()) {
        auto w(workers.pophead());
        w->join(clientio::CLIENTIO); } }

rpcservice::worker::worker(const thread::constoken &t,
                           rpcservice *_owner,
                           socket_t _fd,
                           subscriber &sub)
    : thread(t),
      owner(_owner),
      fd(_fd),
      ds(sub, pub()),
      completedcalls(),
      completedpub() {}

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
    /* Things which the implementation has not yet finished generating
     * a response to. */
    list<response *> incompletecalls;
    buffer outbuf;
    buffer inbuf;
    unsigned outstanding = 0;
    /* Has the client sent a valid HELLO yet? */
    bool helloed = false;
    while (!owner->root->shutdown.ready()) {
        auto s(sub.wait(io));
        if (s == &shutdownsub) continue;
        else if (s == &completedcall) {
            list<response *> incoming;
            attachmentlock.locked([&incoming, this] (mutex_t::token) {
                    incoming.transfer(completedcalls); });
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
                /* It won't reache the responses queue until the
                 * implementation is done with it, so this is a good
                 * place to delete it. */
                delete r;
                /* Things in the outgoing buffer count as complete for
                 * the purposes of applying backpressure to the
                 * client. */
                outstanding--; }
            assert(!outbuf.empty());
            auto r(outbuf.sendfast(fd));
            if (r.isfailure()) {
                auto ss(fd.peer());
                if (ss.issuccess()) r.failure().warn(
                    "sending to " + fields::mk(ss.success()));
                else r.failure().warn("sending to unidentifiable peer?");
                /* Give up on first error.  Most errors are
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
                if (ss.issuccess()) r.failure().warn(
                    "receiving from " + fields::mk(ss.success()));
                else r.failure().warn("receiving from unidentifiable peer?");
                break; }
            while (true) {
                auto rr(wireproto::rx_message::fetch(inbuf));
                if (rr == error::underflowed) break;
                if (rr.isfailure()) {
                    auto ss(fd.peer());
                    if (ss.issuccess()) r.failure().warn(
                        "parsing message from " + fields::mk(ss.success()));
                    else r.failure().warn(
                        "parsing message from unidentifiable peer?");
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
    /* We can't process any more calls.  Any still outstanding will
     * have their results discarded.  Stop them from accessing us
     * again. */
    attachmentlock.locked([&incompletecalls, this] (mutex_t::token) {
            for (auto it(incompletecalls.start()); !it.finished(); it.next()) {
                assert((*it)->owner == this);
                (*it)->owner = NULL; } });
    /* Discard anything which was already complete and which we
     * haven't gotten around to sending yet. */
    responses.transfer(completedcalls);
    while (!responses.empty()) delete responses.pophead(); }

rpcservice::response::response(const wireproto::rx_message &rxm,
                               worker *_owner)
    : inner(rxm),
      owner(_owner) {}

void
rpcservice::response::complete() {
    bool failed = attachmentlock.locked<bool>([this] (mutex_t::token) {
            if (owner == NULL) return true;
            auto notify(owner->completedcalls.empty());
            owner->completedcalls.pushtail(this);
            if (notify) owner->completedpub.publish();
            return false; });
    if (failed) delete this; }

void
rpcservice::response::fail(error err) {
    inner.flush();
    inner.addparam(wireproto::err_parameter, err);
    complete(); }

void
rpcservice::destroy() {
    root->shutdown.set();
    /* The root thread guarantees to terminate quickly once shutdown
     * is set, so this does not need a full clientio token. */
    root->join(clientio::CLIENTIO);
    root = NULL;
    delete this; }

rpcservice::~rpcservice() {
    assert(root == NULL); }
