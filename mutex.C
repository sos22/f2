#include "mutex.H"

#include "backtrace.H"
#include "fuzzsched.H"
#include "logging.H"
#include "timedelta.H"
#include "timestamp.H"

#include "maybe.tmpl"

const fields::field &
mutex_t::token::field() const { return fields::mk(""); }

mutex_t::mutex_t() : heldby(Nothing) { pthread_mutex_init(&mux, NULL); }

mutex_t::~mutex_t() { pthread_mutex_destroy(&mux); }

mutex_t::token
mutex_t::lock() {
    fuzzsched();
    assert(!heldby.isjust() || heldby.__just() != tid::me());
    auto start(timestamp::now());
    while (true) {
        struct timespec end;
        int r = clock_gettime(CLOCK_REALTIME, &end);
        assert(r == 0);
        end.tv_sec++;
        r = pthread_mutex_timedlock(&mux, &end);
        if (r == 0) break;
        assert(r == ETIMEDOUT);
        logmsg(loglevel::info,
               "waiting " + (timestamp::now() - start).field() +
               " for a lock, held by " + heldby.__just().field());
        logmsg(loglevel::info, backtrace().field()); }
    heldby.mkjust(tid::me());
    return token(); }

maybe<mutex_t::token>
mutex_t::trylock() {
    fuzzsched();
    if (pthread_mutex_trylock(&mux) == 0) {
        heldby.mkjust(tid::me());
        return token(); }
    else return Nothing; }

const fields::field &
mutex_t::field() const {
    /* Careful: we might be racing with people acquiring and releasing
     * the lock. This is safe because we know about how the fields are
     * implemented. */
    if (heldby == Nothing) return fields::mk("<unheld>");
    else return "<heldby:"+heldby.__just().field()+">"; }

mutex_t::token
mutex_t::DUMMY() { return token(); }

void
mutex_t::unlock(token *tok) {
    tok->formux(*this);
    tok->release();
    heldby = tid::nonexistent();
    heldby = Nothing;
    pthread_mutex_unlock(&mux);
    fuzzsched(); }

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
mutex_t::trylocked(const std::function<void (maybe<mutex_t::token>)> &f) {
    auto _token(trylock());
    f(_token);
    if (_token != Nothing) unlock(&_token.just()); }
