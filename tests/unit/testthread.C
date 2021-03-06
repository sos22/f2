#include "parsers.H"
#include "test2.H"
#include "thread.H"
#include "timedelta.H"

#include "fields.tmpl"
#include "parsers.tmpl"
#include "test2.tmpl"
#include "thread.tmpl"
#include "waitbox.tmpl"

static testmodule __testthread(
    "thread",
    list<filename>::mk("thread.C", "thread.tmpl", "thread.H"),
    testmodule::BranchCoverage(65_pc),
    "basic", [] {
        auto io(clientio::CLIENTIO);
        class testthr : public thread {
        public: racey<int> &a;
        public: racey<bool> &shutdown;
        public: bool &_died;
        public: testthr(constoken t,
                        int arg,
                        racey<int> &_a,
                        racey<bool> &_shutdown,
                        bool &__died)
            : thread(t),
              a(_a),
              shutdown(_shutdown),
              _died(__died) {
            assert(arg == 901);
            assert(a.load() == 12);
            a.store(97); }
        public: void run(clientio) {
            while (!shutdown.load()) a.store(a.load() + 1); }
        public: ~testthr() { _died = true; } };
        racey<int> cntr(12);
        racey<bool> shutdown(false);
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
        assert(cntr.load() == 97);
        (10_ms).future().sleep(io);
        assert(cntr.load() == 97);
        /* We can unpause it. */
        auto thr2(thr.go());
        assert(thr2 != NULL);
        /* It advances. */
        (10_ms).future().sleep(io);
        assert(cntr.load() > 97);
        /* Publisher doesn't notify while it's running. */
        maybe<thread::deathtoken> token(Nothing);
        {   subscriber sub;
            subscription ds(sub, thr2->pub());
            assert(sub.poll() == &ds);
            (10_ms).future().sleep(io);
            assert(sub.wait(io, timestamp::now()) == NULL);
            assert(thr2->hasdied() == Nothing);
            /* Publisher does notify when it dies. */
            shutdown.store(true);
            assert(sub.wait(io, (10_ms).future()) == &ds);
            token = thr2->hasdied();
            assert(token != Nothing);
            /* But destructor doesn't run yet. */
            assert(!died); }
        /* We can join it. */
        thr2->join(token.just());
        /* And that does run the destructor. */
        assert(died); },
    "join", [] (clientio io) {
        class testthr : public thread {
        public: explicit testthr(constoken token) : thread(token) {}
        public: void run(clientio io) { (500_ms).future().sleep(io); } };
        auto t(thread::start<testthr>(fields::mk("foo")));
        assert(t->hasdied() == Nothing);
        auto tt(timedelta::time([t, io] { t->join(io); }));
        assert(tt > 100_ms);
        assert(tt < 700_ms); },
    "field", [] (clientio io) {
        assert(!strcmp(thread::myname(), "main"));
        class testthr : public thread {
        public: explicit testthr(constoken token) : thread(token) {}
        public: void run(clientio io) { (200_ms).future().sleep(io); } };
        auto t(thread::spawn<testthr>(fields::mk("foo")));
        assert(string(t.unwrap()->field().c_str()) ==
               "<thread:{no tid yet} foo unstarted>");
        auto tt(t.go());
        (100_ms).future().sleep(io);
        string m(tt->field().c_str());
        assert(("<thread:t:" + parsers::intparser<int>() + " foo>")
               .match(m)
               .success() > 0);
        (200_ms).future().sleep(io);
        m = string(tt->field().c_str());
        assert(("<thread:t:" + parsers::intparser<int>() + " foo dead>")
               .match(m)
               .success() > 0);
        tt->join(io); },
    "alien", [] {
        pthread_t thr;
        auto startfn([] (void *) -> void * {
                assert(!strcmp(thread::myname(), "<unknown thread>")); });
        assert(pthread_create(&thr, NULL, startfn, NULL) == 0);
        assert(pthread_join(thr, NULL) == 0); },
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
        public: racey<bool> &shutdown;
        public: testthr(constoken tok,
                        racey<bool> &_shutdown)
            : thread(tok),
              shutdown(_shutdown) {}
        public: void run(clientio) {
            while (!shutdown.load()) pthread_yield(); } };
        /* Check that fieldname changes in expected way as thread
         * moves through its lifecycle. */
        racey<bool> shutdown(false);
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
        shutdown.store(true);
        while (thr2->hasdied() == Nothing) pthread_yield();
        {   auto n(fields::mk(*thr2).c_str());
            assert(strstr(n, "threadname"));
            assert(strstr(n, "dead"));
            assert(!strstr(n, "unstarted")); }
        thr2->join(io); },
    "me", [] (clientio io) {
        class foo : public thread {
        public: waitbox<foo *> &mmm;
        public: foo(constoken t, waitbox<foo *> &m) : thread(t), mmm(m) {}
        public: void run(clientio io) { assert(mmm.get(io) == thread::me()); }};
        waitbox<foo *> www;
        www.set(thread::start<foo>(fields::mk("S"), www));
        www.get(io)->join(io); },
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
