#include <sys/mman.h>

#include "crashhandler.H"
#include "logging.H"
#include "mutex.H"
#include "parsers.H"
#include "spark.H"
#include "test.H"
#include "test2.H"
#include "testassert.H"
#include "thread.H"
#include "timedelta.H"

#include "crashhandler.tmpl"
#include "mutex.tmpl"
#include "parsers.tmpl"
#include "spark.tmpl"
#include "test.tmpl"
#include "test2.tmpl"
#include "testassert.tmpl"
#include "thread.tmpl"

static testmodule __testmutex(
    "mutex",
    list<filename>::mk("mutex.C", "mutex.H", "mutex.tmpl"),
    /* The missing coverage is either syscalls which can't actually
     * fail, or assertions, or stuff which happens in crash handlers
     * (which are in a special process which gcov can't see). */
    testmodule::BranchCoverage(69_pc),
    "basic", [] () {
        /* Spawn a bunch of threads and confirm that only one can
         * hold each lock at a time. */
        const unsigned nr_threads(32);
        const unsigned nr_muxes(32);
        mutex_t muxes[nr_muxes];
        unsigned holders[nr_muxes];
        memset(holders, 0, sizeof(holders));
        volatile bool shutdown(false);
        muxes[0].DUMMY();
        struct thr : public thread {
            mutex_t *const _muxes;
            unsigned *const _holders;
            unsigned const ident;
            volatile bool &_shutdown;
            thr(constoken token,
                mutex_t *__muxes,
                unsigned *__holders,
                unsigned _ident,
                volatile bool &__shutdown)
                : thread(token),
                  _muxes(__muxes),
                  _holders(__holders),
                  ident(_ident),
                  _shutdown(__shutdown) {}
            void run(clientio) {
                while (!_shutdown) {
                    unsigned i((unsigned long)random() % nr_muxes);
                    switch (random() % 4) {
                    case 0: {
                        auto token(_muxes[i].lock());
                        assert(!_holders[i]);
                        _holders[i] = ident;
                        pthread_yield();
                        assert(_holders[i] == ident);
                        _holders[i] = 0;
                        _muxes[i].unlock(&token);
                        break; }
                    case 1:
                        _muxes[i].locked(
                            [this, i] {
                                assert(!_holders[i]);
                                _holders[i] = ident;
                                pthread_yield();
                                assert(_holders[i] == ident);
                                _holders[i] = 0; });
                        break;
                    case 2:
                        _muxes[i].locked(
                            [this, i] (mutex_t::token) {
                                assert(!_holders[i]);
                                _holders[i] = ident;
                                pthread_yield();
                                assert(_holders[i] == ident);
                                _holders[i] = 0; });
                        break;
                    case 3: {
                        long q;
                        long r;
                        r = _muxes[i].locked<long>(
                            [this, i, &q] (mutex_t::token token) {
                                token.formux(_muxes[i]);
                                q = random();
                                assert(!_holders[i]);
                                _holders[i] = ident;
                                pthread_yield();
                                assert(_holders[i] == ident);
                                _holders[i] = 0;
                                return q;});
                        assert(r == q);
                        break; } } } } };
        thr *thrs[nr_threads];
        for (unsigned x = 0; x < nr_threads; x++) {
            thrs[x] = thread::spawn<thr>(fields::mk(x), muxes, holders,
                                         x+1, shutdown).go(); }
        (5_s).future().sleep(clientio::CLIENTIO);
        shutdown = true;
        for (unsigned x = 0; x < nr_threads; x++) {
            thrs[x]->join(clientio::CLIENTIO); } },
    "locked", [] {
        mutex_t mux;
        assert(mux.locked<int>([] { return 7; }) == 7);
        assert(mux.locked<int>([] (mutex_t::token) { return 8; }) == 8); },
#if TESTING
    "deadlockmsg", [] (clientio io) {
        mutex_t mux;
        unsigned nr = 0;
        tests::eventwaiter< ::loglevel> waiter(
            tests::logmsg,
            [&nr] (loglevel level) { if (level >= loglevel::info) nr++; });
        auto tok(mux.lock());
        spark<void> ss([&mux] {
                auto t(mux.lock());
                mux.unlock(&t); });
        (2_s).future().sleep(io);
        mux.unlock(&tok);
        tassert(T(nr) > T(0u));
        tassert(T(nr) < T(4u)); },
#endif
    "field", [] {
        mutex_t mux;
        assert(!strcmp(mux.field().c_str(), "<unheld>"));
        auto t(mux.lock());
        assert(!strcmp(t.field().c_str(), ""));
        assert(("<heldby:t:" + parsers::intparser<unsigned>() + ">")
               .match(mux.field().c_str())
               .success()
               > 0);
        mux.unlock(&t);
        assert(!strcmp(mux.field().c_str(), "<unheld>")); },
    "trylock", [] {
        mutex_t mux;
        auto t1(mux.trylock());
        assert(t1 != Nothing);
        assert(mux.trylock() == Nothing);
        mux.unlock(&t1.just());
        t1 = mux.trylock();
        assert(t1 != Nothing);
        mux.unlock(&t1.just()); },
    "trylocked", [] {
        mutex_t mux;
        mux.trylocked([] (maybe<mutex_t::token> t) {
                assert(t != Nothing); });
        auto t(mux.lock());
        assert(mux.trylocked<bool>([] (maybe<mutex_t::token> tt) {
                    return tt == Nothing; }));
        mux.unlock(&t);
        assert(!mux.trylocked<bool>([] (maybe<mutex_t::token> tt) {
                    return tt == Nothing; })); },
    "crashhandler", [] {
        auto &invoked(crashhandler::allocshared<bool>());
        class cc : public crashhandler {
        public: mutex_t mux;
        public: bool &_invoked;
        public: cc(bool &__invoked)
            : crashhandler(fields::mk("cc")),
              mux(),
              _invoked(__invoked) {}
        public: void doit(crashcontext) override {
            assert(!_invoked);
            mux.locked([this] { _invoked = true; }); } };
        cc handler(invoked);
        assert(!invoked);
        /* Handlers can't get stuck waiting for locks */
        handler.mux.locked([] { crashhandler::invoke(); });
        assert(invoked);
        crashhandler::releaseshared(invoked);
        /* Locks should behave as normal after running the
         * handlers. */
        auto t(handler.mux.lock());
        assert(handler.mux.trylock() == Nothing);
        handler.mux.unlock(&t);
        auto tt(handler.mux.trylock());
        assert(tt != Nothing);
        handler.mux.unlock(&tt.just()); } );
