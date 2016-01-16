#include "mutex.H"

#include "backtrace.H"
#include "crashhandler.H"
#include "fuzzsched.H"
#include "logging.H"
#include "timedelta.H"
#include "timestamp.H"

#include "maybe.tmpl"

const fields::field &
mutex_t::token::field() const { return fields::mk(""); }

mutex_t::mutex_t() : heldby(0) { pthread_mutex_init(&mux, NULL); }

mutex_t::~mutex_t() { assert(heldby == 0); pthread_mutex_destroy(&mux); }

mutex_t::token
mutex_t::lock() {
    /* If we're in crash mode then there's no point in acquiring
     * locks. We've been reduced to a single thread, so if a lock is
     * contended then we'll get suck forever, and crash handlers are
     * supposed to be tolerant of weird memory corruption, anyway. */
    if (crashhandler::crashing()) return token();
    fuzzsched();
    assert(heldby != tid::me().os());
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
               " for a lock, held by " + fields::mk(heldby));
        logmsg(loglevel::info, backtrace().field()); }
    assert(heldby == 0);
    heldby = tid::me().os();
    return token(); }

maybe<mutex_t::token>
mutex_t::trylock() {
    fuzzsched();
    if (pthread_mutex_trylock(&mux) == 0) {
        assert(heldby == 0);
        heldby = tid::me().os();
        return token(); }
    else return Nothing; }

const fields::field &
mutex_t::field() const {
    /* Careful: we might be racing with people acquiring and releasing
     * the lock. This is safe because we know about how the fields are
     * implemented. */
    if (heldby == 0) return fields::mk("<unheld>");
    else return "<heldby:t:" + fields::mk(heldby) + ">"; }

mutex_t::token
mutex_t::DUMMY() { return token(); }

void
mutex_t::unlock(token *tok) {
    /* crash handlers don't bother acquiring locks, so releasing them
     * can only ever cause problems. */
    if (crashhandler::crashing()) return;
    tok->formux(*this);
    tok->release();
    assert(heldby == tid::me().os());
    heldby = 0;
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
