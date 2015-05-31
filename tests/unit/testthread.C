#include "test2.H"
#include "thread.H"
#include "timedelta.H"

#include "fields.tmpl"
#include "test2.tmpl"
#include "thread.tmpl"

static testmodule __testthread(
    "thread",
    list<filename>::mk("thread.C", "thread.tmpl", "thread.H"),
    testmodule::BranchCoverage(65_pc),
    "basic", [] {
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
        public: void run(clientio) { while (!shutdown) a++; }
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
        (10_ms).future().sleep(io);
        assert(cntr == 97);
        /* We can unpause it. */
        auto thr2(thr.go());
        assert(thr2 != NULL);
        /* It advances. */
        (10_ms).future().sleep(io);
        assert(cntr > 97);
        /* Publisher doesn't notify while it's running. */
        maybe<thread::deathtoken> token(Nothing);
        {   subscriber sub;
            subscription ds(sub, thr2->pub());
            (10_ms).future().sleep(io);
            assert(sub.wait(io, timestamp::now()) == NULL);
            assert(thr2->hasdied() == Nothing);
            /* Publisher does notify when it dies. */
            shutdown = true;
            assert(sub.wait(io, (10_ms).future()) == &ds);
            token = thr2->hasdied();
            assert(token != Nothing);
            /* But destructor doesn't run yet. */
            assert(!died); }
        /* We can join it. */
        thr2->join(token.just());
        /* And that does run the destructor. */
        assert(died); },
    "start", [] (clientio io) {
        class testthr : public thread {
        public: bool &go;
        public: testthr(constoken token, bool &_go) : thread(token), go(_go) {}
        public: void run(clientio) {
            assert(!go);
            go = true;
            assert(!strcmp(myname(), "foo")); } };
        bool go(false);
        thread::start<testthr>(fields::mk("foo"), go)->join(io);
        assert(go); },
    "joinpaused", [] {
        class testthr : public thread {
        public: bool &_destructed;
        public: testthr(constoken token,
                        bool &__constructed,
                        bool &__destructed)
            : thread(token),
              _destructed(__destructed) {
            __constructed = true; }
        public: void run(clientio) { abort(); }
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
        assert(destructed); },
    "name", [] (clientio io) {
        class testthr : public thread {
        public: volatile bool &shutdown;
        public: testthr(constoken tok,
                        volatile bool &_shutdown)
            : thread(tok),
              shutdown(_shutdown) {}
        public: void run(clientio) { while (!shutdown) pthread_yield(); } };
        /* Check that fieldname changes in expected way as thread
         * moves through its lifecycle. */
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
        thr2->join(io); },
    "_spawn", [] (clientio io) {
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
              x(t.x) {
            assert(y == 98); }
        public: void run(clientio) { assert(x == 77); } };
        auto t(thread::_spawn<testthr, testthr::token>(
                   fields::mk("_spawn"),
                   [] (const thread::constoken &tt) {
                       return testthr::token(tt, 77); },
                   98));
        auto t2(t.go());
        t2->join(io); });