#include "rpcservice.H"
#include "rpcservice.tmpl"

#include <sys/socket.h>
#include <unistd.h>

#include "listenfd.H"
#include "peername.H"
#include "timedelta.H"

#include "thread.tmpl"

static mutex_t
attachmentlock;

mktupledef(rpcserviceconfig)

class rpcservice::rootthread : public thread {
    /* What fd are we listening on? */
private: listenfd const fd;
    /* What service are we exposing? */
private: rpcservice *const owner;
    /* Set once its time to shut down.  The root thread guarantees to
     * set notacepting quickly after this is set, but does not
     * guarantee to shut down quickly.  Setting this also starts the
     * shutdown protocol on all of the worker threads. */
public:  waitbox<void> shutdown;
    /* Set once we've gone far enough through shutdown that no further
     * clients will be accepted.  Existing clients might still be
     * serviced. */
public:  waitbox<void> notaccepting;
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
public:  mutex_t completionlock;
public:  list<rpcservice::response *> completedcalls;
public:  list<rpcservice::response *> incompletecalls;
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
        ::listen(s, 100) < 0) {
        ::close(s);
        return error::from_errno(); }
    else return s; }

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
      shutdown(),
      notaccepting() {}

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
            workers.pushtail(owner->newworker(l.success(), sub)); }
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
    /* Tell teardown() that we're far enough through to prevent us
     * from accepting more connections. */
    notaccepting.set();
    while (!workers.empty()) {
        auto w(workers.pophead());
        w->join(io); } }

rpcservice::worker *
rpcservice::newworker(socket_t fd, subscriber &sub) {
    auto res(thread::spawn<worker>(
                 "S" + fields::mk(fd.localname()),
                 this,
                 fd,
                 sub));
    return res.go(); }

rpcservice::worker::worker(const thread::constoken &t,
                           rpcservice *_owner,
                           socket_t _fd,
                           subscriber &sub)
    : thread(t),
      owner(_owner),
      fd(_fd),
      ds(sub, pub()),
      completionlock(),
      completedcalls(),
      incompletecalls(),
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
    buffer outbuf;
    buffer inbuf;
    unsigned outstanding = 0;
    while (!owner->root->shutdown.ready()) {
        auto s(sub.wait(io));
        if (s == &shutdownsub) continue;
        else if (s == &completedcall) {
            completionlock.locked([&responses, this] (mutex_t::token) {
                    responses.transfer(completedcalls); });
            if (!responses.empty() && outsub == Nothing) {
                outsub.mkjust(sub, fd.poll(POLLOUT)); } }
        else if (outsub != Nothing && s == &outsub.just()) {
            while (outbuf.avail() < owner->config.maxoutgoingbytes &&
                   !responses.empty()) {
                auto r(responses.pophead());
                r->inner.serialise(outbuf);
                /* It won't reache the responses queue until the
                 * implementation is done with it, so this is a good
                 * place to delete it. */
                delete r;
                /* Things in the outgoing buffer count as complete for
                 * the purposes of apply backpressure to the
                 * client. */
                outstanding--; }
            auto r(outbuf.sendfast(fd));
            if (r.isfailure()) {
                auto ss(fd.peer());
                if (ss.issuccess()) {
                    r.failure().warn("sending to " +
                                     fields::mk(ss.success())); }
                else {
                    r.failure().warn("sending to unidentifiable peer?"); }
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
                if (ss.issuccess()) {
                    r.failure().warn(
                        "receiving from " + fields::mk(ss.success())); }
                else {
                    r.failure().warn("receiving from unidentifiable peer?"); }
                break; }
            while (true) {
                auto rr(wireproto::rx_message::fetch(inbuf));
                if (rr == error::underflowed) break;
                if (rr.isfailure()) {
                    auto ss(fd.peer());
                    if (ss.issuccess()) {
                        r.failure().warn("parsing message from " +
                                         fields::mk(ss.success())); }
                    else {
                        r.failure().warn(
                            "parsing message from unidentifiable peer?"); }
                    goto conndead; }
                auto resp(new response(rr.success(), this));
                attachmentlock.locked([resp, this] (mutex_t::token) {
                        incompletecalls.pushtail(resp); });
                outstanding++;
                owner->call(io, rr.success(), resp); }
            if (outstanding >= owner->config.maxoutstanding) insub = Nothing;
            else insub.just().rearm(); } }
  conndead:
    fd.close();
    /* We can't process any more calls.  Any stil outstanding will
     * have their results discarded.  Stop them from accessing us
     * again. */
    attachmentlock.locked([this] (mutex_t::token) {
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
            owner->completedcalls.pushtail(this);
            for (auto it(owner->incompletecalls.start()); true; it.next()) {
                if (*it == this) {
                    it.remove();
                    break; } }
            owner->completedpub.publish();
            return false; });
    if (failed) delete this; }

void
rpcservice::response::fail(error err) {
    inner.flush();
    inner.addparam(wireproto::err_parameter,err);
    complete(); }

rpcservice::teardowntoken
rpcservice::teardown() {
    root->shutdown.set();
    /* The root thread guarantees to set notaccepting quickly after
     * shutdown gets set, so we don't need a real clientio token
     * here. */
    root->notaccepting.get(clientio::CLIENTIO);
    return teardowntoken(); }

maybe<rpcservice::finishtoken>
rpcservice::finished(teardowntoken) {
    auto t(root->hasdied());
    if (t == Nothing) return Nothing;
    else return finishtoken(t.just()); }

const publisher &
rpcservice::shutdown(teardowntoken) const { return root->pub(); }

void
rpcservice::destroy(finishtoken t) {
    root->join(t.inner);
    delete this; }

void
rpcservice::destroy(clientio io) {
    auto t(teardown());
    maybe<finishtoken> t2(Nothing);
    {   subscriber sub;
        subscription ss(sub, shutdown(t));
        while (t2 == Nothing) {
            sub.wait(io);
            t2 = finished(t); } }
    destroy(t2.just()); }
