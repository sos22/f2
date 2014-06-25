#include "pubsub.H"

#include <sys/poll.h>

#include "fields.H"
#include "logging.H"
#include "test.H"
#include "thread.H"
#include "timedelta.H"

#include "list.tmpl"

class iopollingthread : public threadfn {
private: thread *thrd;
private: mutex_t mux;
private: bool shutdown;
private: list<iosubscription *> what;
private: fd_t readcontrolfd;
private: fd_t writecontrolfd;
private: void run(clientio);
public:  void start();
public:  void attach(iosubscription &);
public:  void detach(iosubscription &);
public:  void stop(clientio);
};
static iopollingthread pollthread;

void
iopollingthread::run(clientio io) {
    int nralloced = 8;
    struct pollfd *pfds = (struct pollfd *)calloc(sizeof(*pfds), nralloced);
    auto token(mux.lock());
    while (!shutdown) {
        pfds[0] = readcontrolfd.poll(POLLIN);
        int nr = 1;
        for (auto it(what.start()); !it.finished(); it.next()) {
            if (nr == nralloced) {
                nralloced += 8;
                pfds = (struct pollfd *)realloc(pfds,
                                                sizeof(*pfds) * nralloced); }
            pfds[nr++] = (*it)->pfd; }
        mux.unlock(&token);
        int r(::poll(pfds, nr, -1));
        token = mux.lock();
        if (r < 0) {
            error::from_errno().fatal(
                "poll()ing for IO with " + fields::mk(nr) + " fds"); }
        int i = 0;
        while (r) {
            assert(i < nr);
            if (!pfds[i].revents) {
                i++;
                continue; }
            r--;
            if (i == 0) {
                /* Control FD is handled as part of the general loop
                 * processing. */
                char b;
                auto readres(readcontrolfd.read(io, &b, 1));
                if (readres.isfailure()) {
                    readres.failure().fatal("reading poller control FD"); }
                i++;
                continue; }
            /* XXX this is not exactly efficient */
            bool found = false;
            for (auto it(what.start()); !it.finished(); it.next()) {
                auto reg(*it);
                if (reg->pfd.fd == pfds[i].fd) {
                    it.remove();
                    reg->registered = false;
                    reg->set();
                    found = true;
                    break; } }
            if (!found) {
                /* This can happen if we race with unregistration in
                   the wrong way.  It should be pretty rare, though,
                   so spit out a warning message when it happens. */
                logmsg(loglevel::debug,
                       "failed to notify subscriber to fd " +
                       fields::mk(pfds[i].fd)); }
            i++; } }
    mux.unlock(&token);
    free(pfds); }

void
iopollingthread::start() {
    assert(thrd == NULL);
    assert(!shutdown);
    auto pr(fd_t::pipe());
    if (pr.isfailure()) {
        pr.failure().fatal("creating polling thread control pipe"); }
    readcontrolfd = pr.success().read;
    writecontrolfd = pr.success().write;
    auto r(thread::spawn(this, &thrd, fields::mk("polling thread")));
    if (r.isjust()) r.just().fatal("starting polling thread"); }

void
iopollingthread::attach(iosubscription &sub) {
    auto token(mux.lock());
    assert(!sub.registered);
    for (auto it(what.start()); !it.finished(); it.next()) {
        assert(*it != &sub); }
    sub.registered = true;
    what.pushtail(&sub);
    mux.unlock(&token);
    /* This could in theory block, if the polling thread has fallen
       massively far behind, but it shouldn't do so very often, and if
       we're that far behind then a bit of backpressure is probably a
       good thing. */
    auto r(writecontrolfd.write(clientio::CLIENTIO, "Y", 1));
    if (r.isfailure()) {
        r.failure().fatal("waking up poller thread for new FD"); } }

void
iopollingthread::detach(iosubscription &sub) {
    auto token(mux.lock());
    if (!sub.registered) {
        /* This can happen sometimes if the iosubscription destructor
           races with the poll thread notification process.  Ignore it
           (beyond spitting out a log message, because it should be
           rare). */
        mux.unlock(&token);
        logmsg(loglevel::debug,
               fields::mk("double unregistered IO subscription"));
    } else {
        bool found = false;
        for (auto it(what.start()); !it.finished(); it.next()) {
            if (*it == &sub) {
                it.remove();
                sub.registered = false;
                found = true;
                break; } }
        assert(found);
        mux.unlock(&token); }
    /* Don't wake polling thread up just to tell them to stop
       listening on this FD; what would be the point? */ }

void
iopollingthread::stop(clientio io) {
    assert(thrd != NULL);
    auto token(mux.lock());
    assert(!shutdown);
    shutdown = true;
    auto r(writecontrolfd.write(io, "X", 1));
    mux.unlock(&token);
    if (r.isfailure()) {
        r.failure().fatal("writing to poller control FD for shutdown"); }
    thrd->join(io);
    thrd = NULL; }

publisher::publisher()
    : mux(),
      subscriptions() {}

void
publisher::publish() {
    auto tok(mux.lock());
    for (auto it(subscriptions.start()); !it.finished(); it.next()) {
        (*it)->set(); }
    mux.unlock(&tok); }

publisher::~publisher() {}

subscriptionbase::subscriptionbase(
    subscriber &_sub)
    : notified(false),
      sub(&_sub) {
    auto subtoken(sub->mux.lock());
    sub->subscriptions.pushtail(this);
    sub->mux.unlock(&subtoken); }

void
subscriptionbase::set() {
    assert(sub);
    if (!notified) {
        auto tok(sub->mux.lock());
        notified = true;
        sub->set(tok);
        sub->mux.unlock(&tok); } }

subscriptionbase::~subscriptionbase() {
    bool r = false;
    auto subtoken(sub->mux.lock());
    for (auto it(sub->subscriptions.start()); !it.finished(); it.next()) {
        if (*it == this) {
            it.remove();
            r = true;
            break; } }
    sub->mux.unlock(&subtoken);
    assert(r); }

subscription::subscription(subscriber &_sub, const publisher &_pub)
    : subscriptionbase(_sub),
      pub(&_pub) {
    auto pubtoken(pub->mux.lock());
    pub->subscriptions.pushtail(this);
    pub->mux.unlock(&pubtoken); }

void
subscription::detach() {
    auto pubtoken(pub->mux.lock());
    bool found = false;
    for (auto it(pub->subscriptions.start()); !it.finished(); it.next()) {
        if (*it == this) {
            it.remove();
            found = true;
            break; } }
    pub->mux.unlock(&pubtoken);
    assert(found);
    pub = NULL; }

subscription::~subscription() {
    assert(!!pub == !!sub);
    if (!pub) return; /* subscriber destructor already did everything
                       * for us */
    detach(); }

iosubscription::iosubscription(
    clientio,
    subscriber &_sub,
    struct pollfd _pfd)
    : subscriptionbase(_sub),
      pfd(_pfd),
      registered(false) {
    rearm(); }

void
iosubscription::rearm() {
    /* Must have been returned by subscriber::wait(), so can't
     * currently be registered. */
    assert(!registered);
    
    /* Fast path if we're already notified or the FD is already
     * ready. */
    if (notified) return;
    struct pollfd p(pfd);
    assert(p.revents == 0);
    int r(::poll(&p, 1, 0));
    if (r > 0 && p.revents) {
        set();
        return; }
    
    pollthread.attach(*this); }

void
iosubscription::detach() {
    /* This isn't properly synchronised with the IO polling thread, so
       we could double unregister.  Polling thread is tolerant of
       that. */
    if (registered) pollthread.detach(*this); }

iosubscription::~iosubscription() {
    detach(); }

void
subscriber::set(mutex_t::token tok) {
    if (!notified) {
        notified = true;
        cond.broadcast(tok); } }

subscriber::subscriber()
    : mux(),
      cond(mux),
      notified(false),
      subscriptions() {}

subscriptionbase *
subscriber::wait(maybe<timestamp> deadline) {
    auto token(mux.lock());
    while (1) {
        notified = false;
        for (auto it(subscriptions.start()); !it.finished(); it.next()) {
            auto r(*it);
            assert(r->sub == this);
            if (r->notified) {
                r->notified = false;
                mux.unlock(&token);
                return r; } }
        while (!notified) {
            auto r(cond.wait(&token, deadline));
            token = r.token;
            if (r.timedout) {
                mux.unlock(&token);
                return NULL; } } } }

subscriber::~subscriber() {
    while (!subscriptions.empty()) {
        auto r(subscriptions.pophead());
        assert(r->sub == this);
        r->detach();
        r->sub = NULL; } }

template class list<subscription *>;
template class list<iosubscription *>;
template class list<subscriptionbase *>;

void
initpubsub() {
    pollthread.start(); }

void
deinitpubsub(clientio io) {
    pollthread.stop(io); }

void
tests::pubsub(clientio io, test &support) {
    auto epsilon(timedelta::milliseconds(10));
    support.msg("Publisher in isolation");
    support.detail("construct/destruct");
    { publisher p; }
    support.detail("empty publish");
    { publisher().publish(); }
    support.msg("Subscriber in isolation");
    support.detail("construct/destruct");
    { subscriber s; }
    support.detail("Empty wait times out");
    assert(subscriber().wait(timestamp::now()) == NULL);
    assert(subscriber().wait(timestamp::now() + epsilon) == NULL);
    support.msg("Cross-class tests");
    support.detail("Basic sub/unsub");
    {   publisher p;
        subscriber s;
        subscription a(s, p);
        assert(s.wait(timestamp::now() + epsilon) == NULL); }
    support.detail("Non-concurrent notifications");
    {   publisher p;
        subscriber s;
        subscription a(s, p);
        p.publish();
        assert(s.wait(timestamp::now()) == &a);
        assert(s.wait(timestamp::now() + epsilon) == NULL); }
    support.detail("Multiple non-concurrent notifications");
    {   publisher p;
        subscriber s;
        subscription a(s, p);
        p.publish();
        p.publish();
        assert(s.wait(timestamp::now()) == &a);
        assert(s.wait(timestamp::now() + epsilon) == NULL);
        p.publish();
        assert(s.wait(timestamp::now()) == &a);
        assert(s.wait(timestamp::now() + epsilon) == NULL); }
    support.detail("Notification after unsubscribe");
    {   publisher p;
        subscriber s;
        {   subscription a(s, p);
            p.publish();
            assert(s.wait(timestamp::now()) == &a); }
        assert(s.wait(timestamp::now() + epsilon) == NULL);
        p.publish();
        assert(s.wait(timestamp::now() + epsilon) == NULL); }
    support.detail("One publisher, one subscriber, multiple subscriptions");
    {   publisher p;
        subscriber s;
        {   subscription a(s, p);
            {   subscription b(s, p);
                p.publish();
                auto f(s.wait(timestamp::now()));
                auto g(s.wait(timestamp::now()));
                auto h(s.wait(timestamp::now() + epsilon));
                assert(f != g);
                assert(f == &a || f == &b);
                assert(g == &a || g == &b);
                assert(h == NULL); }
            assert(s.wait(timestamp::now() + epsilon) == NULL);
            p.publish();
            assert(s.wait(timestamp::now()) == &a);
            assert(s.wait(timestamp::now() + epsilon) == NULL); }
        p.publish();
        assert(s.wait(timestamp::now() + epsilon) == NULL); }
    support.detail("Same, but unsubscribe while notified.");
    {   publisher p;
        subscriber s;
        {   subscription a(s, p);
            {   subscription b(s, p);
                p.publish(); }
            assert(s.wait(timestamp::now()) == &a);
            assert(s.wait(timestamp::now() + epsilon) == NULL);
            p.publish();
            assert(s.wait(timestamp::now()) == &a);
            assert(s.wait(timestamp::now() + epsilon) == NULL); } }
    support.detail("One publisher, two subscribers");
    {   publisher p;
        subscriber s1;
        subscription a(s1, p);
        {   subscriber s2;
            subscription b(s2, p);
            p.publish();
            assert(s1.wait(timestamp::now()) == &a);
            assert(s1.wait(timestamp::now() + epsilon) == NULL);
            assert(s2.wait(timestamp::now()) == &b);
            assert(s2.wait(timestamp::now() + epsilon) == NULL); }
        p.publish();
        assert(s1.wait(timestamp::now()) == &a);
        assert(s1.wait(timestamp::now() + epsilon) == NULL); }
    support.detail("Two publishers, one subscriber");
    {   publisher p1;
        publisher p2;
        subscriber s;
        subscription a(s, p1);
        subscription b(s, p2);
        assert(s.wait(timestamp::now() + epsilon) == NULL);
        p1.publish();
        assert(s.wait(timestamp::now()) == &a);
        assert(s.wait(timestamp::now() + epsilon) == NULL);
        p2.publish();
        assert(s.wait(timestamp::now()) == &b);
        assert(s.wait(timestamp::now() + epsilon) == NULL);
        p1.publish();
        p2.publish();
        auto f(s.wait(timestamp::now()));
        auto g(s.wait(timestamp::now()));
        auto h(s.wait(timestamp::now() + epsilon));
        assert(f != g);
        assert(f == &a || f == &b);
        assert(g == &a || g == &b);
        assert(h == NULL); }
    support.msg("IO subscriptions");
    initpubsub();
    support.detail("basic operation");
    {   subscriber s;
        auto p(fd_t::pipe());
        if (p.isfailure()) p.failure().fatal("creating pipe");
        iosubscription sub(io, s, p.success().read.poll(POLLIN));
        assert(s.wait(timestamp::now() + epsilon) == NULL);
        p.success().write.write(io, "foo", 3);
        
        /* This should arguably be asserting that sub becomes ready
         * immediately, rather than that it becomes ready after
         * epsilon, because it is, but that's much harder to implement
         * and won't matter for any real users of the interface. */
        assert(s.wait(timestamp::now() + epsilon) == &sub);
        
        assert(s.wait(timestamp::now() + epsilon) == NULL);
        sub.rearm();
        assert(s.wait(timestamp::now()) == &sub);
        assert(s.wait(timestamp::now() + epsilon) == NULL);
        char buf[3];
        sub.rearm();
        p.success().read.read(io, buf, 1);
        assert(s.wait(timestamp::now()) == &sub);
        assert(s.wait(timestamp::now() + epsilon) == NULL);
        sub.rearm();
        assert(s.wait(timestamp::now()) == &sub);
        assert(s.wait(timestamp::now() + epsilon) == NULL);
        sub.rearm();
        p.success().read.read(io, buf, 2);
        assert(s.wait(timestamp::now()) == &sub);
        assert(s.wait(timestamp::now() + epsilon) == NULL);
        sub.rearm();
        assert(s.wait(timestamp::now() + epsilon) == NULL); }
    deinitpubsub(io); }
