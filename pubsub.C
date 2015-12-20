#include "pubsub.H"

#include <sys/poll.h>
#include <signal.h>
#include <unistd.h>

#include "buffer.H"
#include "fields.H"
#include "fuzzsched.H"
#include "logging.H"
#include "pair.H"
#include "socket.H"
#include "test.H"
#include "thread.H"

#include "list.tmpl"
#include "orerror.tmpl"
#include "test.tmpl"
#include "thread.tmpl"

class iopollingthread : public thread {
    friend class thread;
private: mutex_t mux;
private: bool shutdown;
private: list<iosubscription *> what;
private: iopollingthread(constoken);
private: void run(clientio);
public:  void start();
public:  void attach(iosubscription &);
public:  void detach(iosubscription &);
public:  void stop(clientio); };

/* pubsub IO polling is done by a global singleton thread. */
static iopollingthread *
pollthread;

/* We use SIGUSR1 to wake up the polling thread from ppoll(). Handler
 * doesn't need to do anything; it's just there to wake us up from the
 * ppoll().  */
static void
sigusr1handler(int) {}

void
iopollingthread::run(clientio) {
    unsigned nralloced = 8;
    struct pollfd *pfds = (struct pollfd *)calloc(nralloced, sizeof(*pfds));
    auto token(mux.lock());
    while (!shutdown) {
        unsigned nr = 0;
        for (auto it(what.start()); !it.finished(); it.next()) {
            if (nr == nralloced) {
                nralloced += 8;
                pfds = (struct pollfd *)realloc(pfds,
                                                sizeof(*pfds) * nralloced); }
            pfds[nr++] = (*it)->pfd; }
        mux.unlock(&token);
        int r;
        {   sigset_t sigs;
            if (::sigprocmask(0, NULL, &sigs) < 0) {
                error::from_errno().fatal("getting sigmask"); }
            assert(::sigismember(&sigs, SIGUSR1));
            ::sigdelset(&sigs, SIGUSR1);
            r = ::ppoll(pfds, nr, NULL, &sigs); }
        if (r < 0 && errno != EINTR) {
            error::from_errno().fatal(
                "poll()ing for IO with " + fields::mk(nr) + " fds"); }
        if (r < 0) r = 0;
        token = mux.lock();
        unsigned i = 0;
        while (r) {
            assert(i < nr);
            if (!pfds[i].revents) {
                i++;
                continue; }
            r--;
            /* XXX this is not exactly efficient */
            bool found = false;
            for (auto it(what.start()); !it.finished(); it.next()) {
                auto reg(*it);
                if (reg->pfd.fd == pfds[i].fd &&
                    (reg->pfd.events & pfds[i].revents) != 0) {
                    it.remove();
                    assert(reg->registered);
                    reg->registered = false;
                    if (COVERAGE) pthread_yield();
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

iopollingthread::iopollingthread(constoken tok)
    : thread(tok),
      mux(),
      shutdown(false),
      what() { }

void
iopollingthread::attach(iosubscription &sub) {
    auto token(mux.lock());
    assert(!sub.registered);
    for (auto it(what.start()); !it.finished(); it.next()) assert(*it != &sub);
    sub.registered = true;
    what.pushtail(&sub);
    mux.unlock(&token);
    pollthread->kill(SIGUSR1).fatal("waking up poller thread for new FD"); }

void
iopollingthread::detach(iosubscription &sub) {
    auto token(mux.lock());
    if (!sub.registered) {
        mux.unlock(&token);
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
    pollthread->kill(SIGUSR1).fatal("waking up poller thread for new FD"); }

void
iopollingthread::stop(clientio io) {
    mux.locked([this] {
            assert(!shutdown);
            shutdown = true; });
    pollthread->kill(SIGUSR1).fatal("waking up poller thread for shutdown");
    join(io); }

publisher::publisher()
    : mux(),
      subscriptions() {}

void
publisher::publish() {
    unsigned cntr = 0;
    auto tok(mux.lock());
    for (auto it(subscriptions.start()); !it.finished(); it.next()) {
        if (++cntr % 10000 == 0) {
            logmsg(loglevel::error, "wake lots of subs " + fields::mk(cntr));}
        (*it)->set(); }
    mux.unlock(&tok); }

publisher::~publisher() {}

subscriptionbase::subscriptionbase(
    subscriber &_sub,
    void *_data)
    : notified(false),
      sub(&_sub),
      data(_data) {
    auto subtoken(sub->mux.lock());
    sub->subscriptions.pushtail(this);
    sub->mux.unlock(&subtoken); }

void
subscriptionbase::set() {
    assert(sub);
    auto tok(sub->mux.lock());
    if (!notified) {
        notified = true;
        sub->nrnotified++;
        if (sub->nrnotified == 1) sub->cond.broadcast(tok); }
    sub->mux.unlock(&tok); }

subscriptionbase::~subscriptionbase() {
    if (!sub) return;
    auto token(sub->mux.lock());
    bool found = false;
    for (auto it(sub->subscriptions.start()); !it.finished(); it.next()) {
        if (*it == this) {
            found = true;
            it.remove();
            break; } }
    if (notified) sub->nrnotified--;
    sub->mux.unlock(&token);
    assert(found); }

subscription::subscription(subscriber &_sub, const publisher &_pub, void *_data)
    : subscriptionbase(_sub, _data),
      pub(&_pub) {
    auto pubtoken(pub->mux.lock());
    pub->subscriptions.pushtail(this);
    pub->mux.unlock(&pubtoken);
    set(); }

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
    if (pub) detach(); }

iosubscription::iosubscription(
    subscriber &_sub,
    struct pollfd _pfd)
    : subscriptionbase(_sub),
      pfd(_pfd),
      registered(false) {
    assert(pollthread != NULL);
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
    
    pollthread->attach(*this); }

void
iosubscription::detach() {
    /* This isn't properly synchronised with the IO polling thread, so
       we could double unregister.  Polling thread is tolerant of
       that. */
    tests::iosubdetachrace.trigger();
    pollthread->detach(*this); }

iosubscription::~iosubscription() {
    detach(); }

subscriber::subscriber()
    : mux(),
      cond(mux),
      nrnotified(0),
      subscriptions() {}

subscriptionbase *
subscriber::wait(clientio io, maybe<timestamp> deadline) {
    auto token(mux.lock());
    while (1) {
        while (nrnotified == 0) {
            auto r(cond.wait(io, &token, deadline));
            token = r.token;
            if (r.timedout) {
                mux.unlock(&token);
                fuzzsched();
                return NULL; } }
        unsigned cntr = 0;
        for (auto it(subscriptions.start()); true; it.next()) {
            assert(!it.finished());
            auto r(*it);
            assert(r->sub == this);
            if (r->notified) {
                r->notified = false;
                nrnotified--;
                mux.unlock(&token);
                fuzzsched();
                return r; }
            if (++cntr % 1000 == 0) {
                logmsg(loglevel::error,
                       "very long subs list " + fields::mk(cntr)); } } } }

subscriptionbase *
subscriber::poll() {
    /* Setting a deadline of now means that the wait will never block,
       so doesn't need a clientio token. */
    return wait(clientio::CLIENTIO, timestamp::now()); }

subscriber::~subscriber() {
    while (!subscriptions.empty()) {
        auto r(subscriptions.pophead());
        assert(r->sub == this);
        if (r->notified) nrnotified--;
        r->detach();
        r->sub = NULL; }
    assert(nrnotified == 0); }

void
initpubsub() {
    if (pollthread != NULL) return;
    auto ss(::signal(SIGUSR1, sigusr1handler));
    if (ss == SIG_ERR) error::from_errno().fatal("installing sigusr1 handler");
    if (ss != SIG_DFL) error::toolate.fatal("conflicting SIGUSR1 handlers");
    /* Block SIGUSR1 now so that it's blocked in the new thread from
     * the very beginning, to avoid silly races. */
    sigset_t oldsi;
    sigset_t blockset;
    ::sigemptyset(&blockset);
    ::sigaddset(&blockset, SIGUSR1);
    if (::sigprocmask(SIG_BLOCK, &blockset, &oldsi) < 0) {
        error::from_errno().fatal("blocking SIGUSR1"); }
    pollthread = thread::start<iopollingthread>(fields::mk("iopoll"));
    if (::sigprocmask(SIG_SETMASK, &oldsi, NULL) < 0) {
        error::from_errno().fatal("unblocking SIGUSR1"); } }

void
deinitpubsub(clientio io) {
    if (pollthread == NULL) return;
    pollthread->stop(io);
    auto ss(::signal(SIGUSR1, SIG_DFL));
    if (ss == SIG_ERR) error::from_errno().fatal("restoring sigusr1 handler");
    if (ss != sigusr1handler) error::toolate.fatal("SIGUSR1 handler changed");
    pollthread = NULL; }

tests::event<void> tests::iosubdetachrace;
