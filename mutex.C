#include "mutex.H"

#include "fields.H"
#include "test.H"
#include "thread.H"
#include "timedelta.H"

#include "maybe.tmpl"
#include "mutex.tmpl"
#include "thread.tmpl"

mutex_t::mutex_t()
    : heldby(Nothing) {
    pthread_mutex_init(&mux, NULL); }

mutex_t::~mutex_t()
{
    pthread_mutex_destroy(&mux);
}

mutex_t::token
mutex_t::lock()
{
    pthread_mutex_lock(&mux);
    heldby.mkjust(tid::me());
    return token();
}

mutex_t::token
mutex_t::DUMMY() { return token(); }

void
mutex_t::unlock(token *tok)
{
    tok->formux(*this);
    tok->release();
    heldby = Nothing;
    pthread_mutex_unlock(&mux);
}

void
mutex_t::locked(const std::function<void (mutex_t::token)> &f) {
    auto _token(lock());
    f(_token);
    unlock(&_token); }

void
mutex_t::locked(const std::function<void (void)> &f) {
    auto _token(lock());
    f();
    unlock(&_token); }

void
tests::mutex() {
    testcaseV("mutex", "basic", [] () {
            /* Spawn a bunch of threads and confirm that only one can
             * hold each lock at a time. */
            const unsigned nr_threads(32);
            const unsigned nr_muxes(32);
            mutex_t muxes[nr_muxes];
            unsigned holders[nr_muxes];
            memset(holders, 0, sizeof(holders));
            volatile bool shutdown(false);
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
                        switch (random() % 3) {
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
                                [this, i] (mutex_t::token token) {
                                    token.formux(_muxes[i]);
                                    assert(!_holders[i]);
                                    _holders[i] = ident;
                                    pthread_yield();
                                    assert(_holders[i] == ident);
                                    _holders[i] = 0; });
                            break;
                        case 2: {
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
            (timestamp::now() + timedelta::seconds(5))
                .sleep(clientio::CLIENTIO);
            shutdown = true;
            for (unsigned x = 0; x < nr_threads; x++) {
                thrs[x]->join(clientio::CLIENTIO); } }); }
