#include "thread.H"

#include <sys/prctl.h>

#include "error.H"
#include "fields.H"
#include "profile.H"
#include "test.H"
#include "tid.H"
#include "timedelta.H"
#include "util.H"

#ifndef COVERAGESKIP
/* Not sure why llvm+gcov think that there's code to test on this
 * line, but there isn't any, so COVERAGESKIP it. */
#endif

#include "list.tmpl"
#include "thread.tmpl"
#include "waitbox.tmpl"

static __thread const char *this_name;

thread::thread(const thread::constoken &token)
    : thr(),
      started(false),
      tid_(),
      name(strdup(token.name.c_str())),
      dead(false) {}

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
    this_name = thr->name;
    prctl(PR_SET_NAME, (unsigned long)this_name, 0, 0);
    profilenewthread();
    thr->run(clientio::CLIENTIO);
    profileendthread();
    /* Tell subscribers that we died. */
    storerelease(&thr->dead, true);
    thr->_pub.publish();
    return NULL; }

maybe<thread::deathtoken>
thread::hasdied() const {
    if (loadacquire(dead)) return deathtoken();
    else return Nothing; }

const publisher &
thread::pub() const { return _pub; }

void
thread::join(deathtoken) {
    /* We guarantee that the thread shuts down quickly once dead is
     * set, and that no death tokens can be created until dead is set,
     * so this is guaranteed to finish quickly. */
    assert(loadacquire(dead));
    int r = pthread_join(thr, NULL);
    if (r) error::from_errno(r).fatal("joining thread " + fields::mk(name));
    delete this; }

void
thread::join(clientio io) {
    if (!started) {
        delete this;
        return; }
    auto token(hasdied());
    if (token == Nothing) {
        subscriber sub;
        subscription ds(sub, pub());
        token = hasdied();
        while (token == Nothing) {
            auto d(sub.wait(io));
            assert(d == &ds);
            token = hasdied(); } }
    join(token.just()); }

thread::~thread() { free((void *)name); }

const fields::field &
fields::mk(const thread &thr) {
    const field *acc(&fields::mk("<thread:"));
    auto tid(thr.tid_.poll());
    if (tid.isjust()) {
        acc = &(*acc + fields::mk(tid.just())); }
    else {
        acc = &(*acc + fields::mk("{no tid yet}")); }
    acc = &(*acc + " " + fields::mk(thr.name));
    if (loadacquire(thr.dead)) acc = &(*acc + " dead");
    if (!thr.started) acc = &(*acc + " unstarted");
    return *acc + ">"; }

const char *
thread::myname() { return this_name ?: "<unknown thread>"; }

void
tests::thread() {
    testcaseV("thread", "basic", [] {
            auto io(clientio::CLIENTIO);
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
            (timestamp::now() + timedelta::milliseconds(10)).sleep(io);
            assert(cntr == 97);
            /* We can unpause it. */
            auto thr2(thr.go());
            assert(thr2 != NULL);
            /* It advances. */
            (timestamp::now() + timedelta::milliseconds(10)).sleep(io);
            assert(cntr > 97);
            /* Publisher doesn't notify while it's running. */
            maybe<thread::deathtoken> token(Nothing);
            {   subscriber sub;
                subscription ds(sub, thr2->pub());
                (timestamp::now() + timedelta::milliseconds(10)).sleep(io);
                assert(sub.wait(io, timestamp::now()) == NULL);
                assert(thr2->hasdied() == Nothing);
                /* Publisher does notify when it dies. */
                shutdown = true;
                assert(sub.wait(
                           io,
                           timestamp::now() + timedelta::milliseconds(10))
                       == &ds);
                token = thr2->hasdied();
                assert(token != Nothing);
                /* But destructor doesn't run yet. */
                assert(!died); }
            /* We can join it. */
            thr2->join(token.just());
            /* And that does run the destructor. */
            assert(died); });

    testcaseIO("thread", "start", [] (clientio io) {
            class testthr : public thread {
            public: bool &go;
            public: testthr(constoken token, bool &_go)
                : thread(token),
                  go(_go) {}
            public: void run(clientio) {
                assert(!go);
                go = true;
                assert(!strcmp(myname(), "foo")); } };
            bool go(false);
            thread::start<testthr>(fields::mk("foo"), go)->join(io);
            assert(go); });

    testcaseV("thread", "joinpaused", [] () {
            class testthr : public thread {
            public: bool &_destructed;
            public: testthr(constoken token,
                            bool &__constructed,
                            bool &__destructed)
                : thread(token),
                  _destructed(__destructed) {
                __constructed = true; }
            public: void run(clientio) {abort();}
            public: ~testthr() { _destructed = true; } };
            bool constructed = false;
            bool destructed = false;
            /* We can join a thread before unpausing it */
            auto thr(thread::spawn<testthr>(fields::mk("bar"),
                                            constructed,
                                            destructed));
            assert(constructed);
            assert(!destructed);
            assert(&thr.unwrap()->_destructed == &destructed);
            /* We can stop it again. */
            thr.destroy();
            assert(constructed);
            assert(destructed); });

    testcaseIO("thread", "name", [] (clientio io) {
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
            thr2->join(io); });

    testcaseIO("thread", "_spawn", [] (clientio io) {
            class testthr : public thread {
            public: class token {
                public: thread::constoken inner;
                public: int x;
                public: token(const thread::constoken &_inner,
                              int _x)
                    : inner(_inner),
                      x(_x) {} };
            public: const int x;
            public: testthr(const token &t, int y)
                : thread(t.inner),
                  x(t.x) { assert(y == 98); }
            public: void run(clientio) {
                assert(x == 77); } };
            auto t(thread::_spawn<testthr, testthr::token>(
                       fields::mk("_spawn"),
                       [] (const thread::constoken &tt) {
                           return testthr::token(tt, 77); },
                       98));
            auto t2(t.go());
            t2->join(io); });
}
