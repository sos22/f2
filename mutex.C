#include "mutex.H"

#include "fields.H"
#include "test.H"
#include "thread2.H"
#include "timedelta.H"

#include "thread2.tmpl"

mutex_t::mutex_t()
{
    pthread_mutex_init(&mux, NULL);
}

mutex_t::~mutex_t()
{
    pthread_mutex_destroy(&mux);
}

mutex_t::token
mutex_t::lock()
{
    pthread_mutex_lock(&mux);
    return token();
}

void
mutex_t::unlock(token *tok)
{
    tok->formux(*this);
    tok->release();
    pthread_mutex_unlock(&mux);
}

void
tests::mutex() {
    testcaseV("mutex", "basic", [] () {
            /* Spawn a bunch of threads and confirm that only one can
             * hold each lock at a time. */
            const unsigned nr_threads(32);
            const unsigned nr_muxes(32);
            mutex_t muxes[nr_muxes];
            int holders[nr_muxes];
            memset(holders, 0, sizeof(holders));
            volatile bool shutdown(false);
            struct thr : public thread2 {
                mutex_t *const _muxes;
                int *const _holders;
                int const ident;
                volatile bool &_shutdown;
                thr(constoken token,
                    mutex_t *__muxes,
                    int *__holders,
                    int _ident,
                    volatile bool &__shutdown)
                    : thread2(token),
                      _muxes(__muxes),
                      _holders(__holders),
                      ident(_ident),
                      _shutdown(__shutdown) {}
                void run(clientio) {
                    while (!_shutdown) {
                        int i((int)random() % nr_muxes);
                        auto token(_muxes[i].lock());
                        assert(!_holders[i]);
                        _holders[i] = ident;
                        pthread_yield();
                        assert(_holders[i] == ident);
                        _holders[i] = 0;
                        _muxes[i].unlock(&token); } } };
            thr *thrs[nr_threads];
            for (unsigned x = 0; x < nr_threads; x++) {
                unsigned y(x+1);
                thrs[x] = thread2::spawn<thr>(fields::mk(x), muxes, holders,
                                              y, shutdown).go(); }
            (timestamp::now() + timedelta::seconds(5)).sleep();
            shutdown = true;
            for (unsigned x = 0; x < nr_threads; x++) {
                thrs[x]->join(clientio::CLIENTIO); } }); }
