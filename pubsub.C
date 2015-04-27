#include "pubsub.H"

#include <sys/poll.h>
#include <unistd.h>

#include "buffer.H"
#include "fields.H"
#include "logging.H"
#include "pair.H"
#include "socket.H"
#include "test.H"
#include "thread.H"

#include "list.tmpl"
#include "test.tmpl"
#include "thread.tmpl"

class iopollingthread : public thread {
    friend class thread;
private: mutex_t mux;
private: bool shutdown;
private: list<iosubscription *> what;
private: fd_t readcontrolfd;
private: fd_t writecontrolfd;
private: iopollingthread(constoken);
private: void run(clientio);
public:  void start();
public:  void attach(iosubscription &);
public:  void detach(iosubscription &);
public:  void stop(clientio);
};

/* Not sure why llvm seems to think that there's code associated with
 * this, but it does, and there doesn't really seem to be any way of
 * running it, so COVERAGESKIP it. */
#ifndef COVERAGESKIP
static iopollingthread *pollthread;
#endif

void
iopollingthread::run(clientio io) {
    unsigned nralloced = 8;
    struct pollfd *pfds = (struct pollfd *)calloc(nralloced, sizeof(*pfds));
    auto token(mux.lock());
    while (!shutdown) {
        pfds[0] = readcontrolfd.poll(POLLIN);
        unsigned nr = 1;
        for (auto it(what.start()); !it.finished(); it.next()) {
            if (nr == nralloced) {
                nralloced += 8;
                pfds = (struct pollfd *)realloc(pfds,
                                                sizeof(*pfds) * nralloced); }
            pfds[nr++] = (*it)->pfd; }
        mux.unlock(&token);
        int r(::poll(pfds, nr, -1));
        token = mux.lock();
        if (!COVERAGE && r < 0) {
            error::from_errno().fatal(
                "poll()ing for IO with " + fields::mk(nr) + " fds"); }
        unsigned i = 0;
        while (r) {
            assert(i < nr);
            if (!pfds[i].revents) {
                i++;
                continue; }
            r--;
            if (i == 0) {
                /* Control FD is handled as part of the general loop
                 * processing, so just clear the message. */
                char b;
                auto readres(readcontrolfd.read(io, &b, 1));
                if (!COVERAGE && readres.isfailure()) {
                    readres.failure().fatal("reading poller control FD"); }
                i++;
                continue; }
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
      what(),
      readcontrolfd(),
      writecontrolfd() {
    assert(!shutdown);
    auto pr(fd_t::pipe());
    if (!COVERAGE && pr.isfailure()) {
        pr.failure().fatal("creating polling thread control pipe"); }
    readcontrolfd = pr.success().read;
    writecontrolfd = pr.success().write; }

void
iopollingthread::attach(iosubscription &sub) {
    auto token(mux.lock());
    assert(!sub.registered);
    if (!COVERAGE) {
        for (auto it(what.start()); !it.finished(); it.next()) {
            assert(*it != &sub); } }
    sub.registered = true;
    what.pushtail(&sub);
    mux.unlock(&token);
    /* This could in theory block, if the polling thread has fallen
       massively far behind, but it shouldn't do so very often, and if
       we're that far behind then a bit of backpressure is probably a
       good thing. */
    auto r(writecontrolfd.write(clientio::CLIENTIO, "Y", 1));
    if (!COVERAGE && r.isfailure()) {
        r.failure().fatal("waking up poller thread for new FD"); } }

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
    /* Surprise!  If you have a poll() which is listening on FDs A and
     * B and you close B at about the same time as A becomes readable
     * the kernel wil occasionally completely fail to wake you up.
     * Not waking up for B being closed is pretty reasonable, but not
     * waking up for A becoming readable seems like a bug (although
     * it's not one I've been able to reproduce in a small, controlled
     * test case).  In either case, waking the iopoll thread here
     * seems to fix things. */
    auto r(writecontrolfd.write(clientio::CLIENTIO, "Z", 1));
    if (!COVERAGE && r.isfailure()) {
        r.failure().fatal("waking up poller thread for lost FD"); } }

void
iopollingthread::stop(clientio io) {
    auto token(mux.lock());
    assert(!shutdown);
    shutdown = true;
    auto r(writecontrolfd.write(io, "X", 1));
    mux.unlock(&token);
    if (!COVERAGE && r.isfailure()) {
        r.failure().fatal("writing to poller control FD for shutdown"); }
    join(io); }

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
    if (!notified) {
        auto tok(sub->mux.lock());
        notified = true;
        sub->set(tok);
        sub->mux.unlock(&tok); } }

subscriptionbase::~subscriptionbase() {
    if (!sub) return;
    auto token(sub->mux.lock());
    bool found = false;
    for (auto it(sub->subscriptions.start()); !it.finished(); it.next()) {
        if (*it == this) {
            found = true;
            it.remove();
            break; } }
    sub->mux.unlock(&token);
    assert(found); }

subscription::subscription(subscriber &_sub, const publisher &_pub, void *_data)
    : subscriptionbase(_sub, _data),
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
    if (pub) detach(); }

iosubscription::iosubscription(
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
subscriber::wait(clientio io, maybe<timestamp> deadline) {
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
            auto r(cond.wait(io, &token, deadline));
            token = r.token;
            if (r.timedout) {
                mux.unlock(&token);
                return NULL; } } } }

subscriptionbase *
subscriber::poll() {
    /* Setting a deadline of now means that the wait will never block,
       so doesn't need a clientio token. */
    return wait(clientio::CLIENTIO, timestamp::now()); }

subscriber::~subscriber() {
    while (!subscriptions.empty()) {
        auto r(subscriptions.pophead());
        assert(r->sub == this);
        r->detach();
        r->sub = NULL; } }

void
initpubsub() {
    pollthread = thread::spawn<iopollingthread>(fields::mk("iopoll")).go(); }

void
deinitpubsub(clientio io) {
    pollthread->stop(io); }

tests::event<void> tests::iosubdetachrace;
