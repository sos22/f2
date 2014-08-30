#include "thread.H"

#include <sys/prctl.h>

#include "error.H"
#include "fields.H"
#include "test.H"
#include "tid.H"
#include "timedelta.H"
#include "util.H"

#include "list.tmpl"
#include "thread.tmpl"
#include "waitbox.tmpl"

static mutex_t
detachlock;

thread::thread(const thread::constoken &token)
    : thr(),
      started(false),
      tid_(),
      name(strdup(token.name.c_str())),
      dead(false),
      subscribers() {}

void
thread::go() {
    assert(!started);
    started = true;
    int r = pthread_create(&thr,
                           NULL,
                           pthreadstart,
                           this);
    if (r) error::from_errno(r).fatal("spawn thread " + fields::mk(name)); }

void *
thread::pthreadstart(void *_this) {
    thread *thr = static_cast<thread *>(_this);
    assert(thr->started == true);
    /* pthread API doesn't give a good way of getting tid from parent
       process, so do it from here instead. */
    thr->tid_.set(tid::me());
    prctl(PR_SET_NAME, (unsigned long)thr->name, 0, 0);
    thr->run(clientio::CLIENTIO);
    /* Tell subscribers that we died. */
    auto token(detachlock.lock());
    storerelease(&thr->dead, true);
    for (auto it(thr->subscribers.start()); !it.finished(); it.remove()) {
        (*it)->owner = NULL;
        (*it)->set(); }
    detachlock.unlock(&token);
    return NULL; }

maybe<thread::deathtoken>
thread::hasdied() const {
    if (loadacquire(dead)) return deathtoken();
    else return Nothing; }

thread::deathsubscription::deathsubscription(subscriber &_sub,
                                              thread *_owner)
    : subscriptionbase(_sub),
      owner(_owner) {
    auto token(detachlock.lock());
    owner->subscribers.pushtail(this);
    detachlock.unlock(&token);
    if (loadacquire(_owner->dead)) set(); }

void
thread::deathsubscription::detach() {
    auto token(detachlock.lock());
    if (owner) {
        bool found = false;
        for (auto it(owner->subscribers.start()); !it.finished(); it.next()) {
            if (*it == this) {
                it.remove();
                found = true;
                break; } }
        assert(found);
        owner = NULL; }
    detachlock.unlock(&token); }

thread::deathsubscription::~deathsubscription() {
    detach(); }

void
thread::join(deathtoken) {
    assert(loadacquire(dead));
    int r = pthread_join(thr, NULL);
    if (r) error::from_errno(r).fatal("joining thread " + fields::mk(name));
    auto token(detachlock.lock());
    while (!subscribers.empty()) {
        auto i = subscribers.pophead();
        if (i->owner == NULL) continue;
        assert(i->owner == this);
        i->owner = NULL; }
    detachlock.unlock(&token);
    delete this; }

void
thread::join(clientio io) {
    if (!started) {
        delete this;
        return; }
    subscriber sub;
    deathsubscription ds(sub, this);
    auto d(sub.wait(io));
    assert(d == &ds);
    auto token(hasdied());
    assert(token != Nothing);
    join(token.just()); }

thread::~thread() {
    assert(subscribers.empty());
    free((void *)name); }

const fields::field &
fields::mk(const thread &thr) {
    const field *acc(&fields::mk("<thread:"));
    if (thr.tid_.ready()) {
        acc = &(*acc + fields::mk(thr.tid_.get(clientio::CLIENTIO))); }
    else {
        acc = &(*acc + fields::mk("{no tid yet}")); }
    acc = &(*acc + " " + fields::mk(thr.name));
    if (loadacquire(thr.dead)) acc = &(*acc + " dead");
    if (!thr.started) acc = &(*acc + " unstarted");
    return *acc + ">"; }

void
tests::thread() {
    testcaseV("thread", "basic", [] () {
            class testthr : public thread {
            public: volatile int &a;
            public: volatile bool &shutdown;
            public: bool &_died;
            public: testthr(constoken t,
                            int arg,
                            volatile int &_a,
                            volatile bool &_shutdown,
                            bool &__died)
                : thread(t),
                  a(_a),
                  shutdown(_shutdown),
                  _died(__died) {
                assert(arg == 901);
                assert(a == 12);
                a = 97; }
            public: void run(clientio) {
                while (!shutdown) a++; }
            public: ~testthr() { _died = true; } };

            volatile int cntr = 12;
            volatile bool shutdown = false;
            bool died = false;
            /* We can create a thread. */
            int whatever = 901;
            auto thr(thread::spawn<testthr>(fields::mk("foo"),
                                            whatever,
                                            cntr,
                                            shutdown,
                                            died));
            /* The constructor runs immediately, but the run() method
             * doesn't. */
            assert(cntr == 97);
            (timestamp::now() + timedelta::milliseconds(10)).sleep();
            assert(cntr == 97);
            /* We can unpause it. */
            auto thr2(thr.go());
            assert(thr2 != NULL);
            /* It advances. */
            (timestamp::now() + timedelta::milliseconds(10)).sleep();
            assert(cntr > 97);
            /* Publisher doesn't notify while it's running. */
            subscriber sub;
            thread::deathsubscription ds(sub, thr2);
            (timestamp::now() + timedelta::milliseconds(10)).sleep();
            assert(sub.wait(clientio::CLIENTIO,
                            timestamp::now()) == NULL);
            assert(thr2->hasdied() == Nothing);
            /* Publisher does notify when it dies. */
            shutdown = true;
            assert(sub.wait(
                       clientio::CLIENTIO,
                       timestamp::now() + timedelta::milliseconds(10)) == &ds);
            auto token(thr2->hasdied());
            assert(token != Nothing);
            /* But destructor doesn't run yet. */
            assert(!died);
            /* We can join it. */
            thr2->join(token.just());
            /* And that does run the destructor. */
            assert(died); });

    testcaseV("thread", "joinpaused", [] () {
            class testthr : public thread {
            public: bool &_runran;
            public: bool &_destructed;
            public: testthr(constoken token,
                            bool &__constructed,
                            bool &__runran,
                            bool &__destructed)
                : thread(token),
                  _runran(__runran),
                  _destructed(__destructed) {
                __constructed = true; }
#ifndef COVERAGESKIP
            public: void run(clientio) {_runran = true;}
#endif
            public: ~testthr() { _destructed = true; } };
            bool constructed = false;
            bool runran = false;
            bool destructed = false;
            /* We can join a thread before unpausing it */
            auto thr(thread::spawn<testthr>(fields::mk("bar"),
                                            constructed,
                                            runran,
                                            destructed));
            assert(constructed);
            assert(!runran);
            assert(!destructed);
            assert(&thr.unwrap()->_runran == &runran);
            /* We can stop it again. */
            thr.destroy();
            assert(constructed);
            assert(!runran);
            assert(destructed); });

    testcaseV("thread", "autonotify", [] () {
            class testthr : public thread {
            public: testthr(constoken token)
                : thread(token) {}
            public: void run(clientio) {} };
            /* death subscriptions should auto-notify when attached to
               things which are already dead. */
            auto thr(thread::spawn<testthr>(fields::mk("wibble")).go());
            while (thr->hasdied() == Nothing) pthread_yield();
            subscriber sub;
            assert(sub.wait(clientio::CLIENTIO, timestamp::now()) == NULL);
            thread::deathsubscription ds(sub, thr);
            assert(sub.wait(clientio::CLIENTIO, timestamp::now()) == &ds);
            thr->join(clientio::CLIENTIO); });

    testcaseV("thread", "detach", [] () {
            class testthr : public thread {
            public: volatile bool &shutdown;
            public: testthr(constoken token,
                            volatile bool &_shutdown)
                : thread(token),
                  shutdown(_shutdown) {}
            public: void run(clientio) { while (!shutdown) {} } };
            /* It should be possible to detach a death subscription to
               stop receiving further notifications. */
            volatile bool shutdown = false;
            auto thr(thread::spawn<testthr>(fields::mk("foo"), shutdown).go());
            subscriber sub;
            thread::deathsubscription ds(sub, thr);
            assert(sub.poll() == NULL);
            ds.detach();
            assert(sub.poll() == NULL);
            shutdown = true;
            while (thr->hasdied() == Nothing) pthread_yield();
            assert(sub.poll() == NULL);
            thr->join(clientio::CLIENTIO);
            assert(sub.poll() == NULL); });

    testcaseV("thread", "name", [] () {
            class testthr : public thread {
            public: volatile bool &shutdown;
            public: testthr(constoken tok,
                            volatile bool &_shutdown)
                : thread(tok),
                  shutdown(_shutdown) {}
            public: void run(clientio) { while (!shutdown) pthread_yield(); } };
            /* Check that fieldname changes in expected way as thread
               moves through its lifecycle. */
            volatile bool shutdown = false;
            auto thr1(thread::spawn<testthr>(
                          fields::mk("threadname"),
                          shutdown));
            {   auto n(fields::mk(*thr1.unwrap()).c_str());
                assert(strstr(n, "threadname"));
                assert(!strstr(n, "dead"));
                assert(strstr(n, "unstarted")); }
            auto thr2(thr1.go());
            {   auto n(fields::mk(*thr2).c_str());
                assert(strstr(n, "threadname"));
                assert(!strstr(n, "dead"));
                assert(!strstr(n, "unstarted")); }
            shutdown = true;
            while (thr2->hasdied() == Nothing) pthread_yield();
            {   auto n(fields::mk(*thr2).c_str());
                assert(strstr(n, "threadname"));
                assert(strstr(n, "dead"));
                assert(!strstr(n, "unstarted")); }
            thr2->join(clientio::CLIENTIO); }); }
