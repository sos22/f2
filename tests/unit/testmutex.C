#include "mutex.H"
#include "parsers.H"
#include "test2.H"
#include "thread.H"
#include "timedelta.H"

#include "mutex.tmpl"
#include "parsers.tmpl"
#include "test2.tmpl"
#include "thread.tmpl"

static testmodule __testmutex(
    "mutex",
    list<filename>::mk("mutex.C", "mutex.H", "mutex.tmpl"),
    testmodule::BranchCoverage(65_pc),
    testmodule::LineCoverage(85_pc),
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
    "field", [] {
        mutex_t mux;
        assert(!strcmp(mux.field().c_str(), "<unheld>"));
        auto t(mux.lock());
        assert(("<heldby:t:" + parsers::intparser<unsigned>() + ">")
               .match(mux.field().c_str())
               .success()
               > 0);
        mux.unlock(&t);
        assert(!strcmp(mux.field().c_str(), "<unheld>")); });
