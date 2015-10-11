#include "mutex.H"

#include "fuzzsched.H"

#include "maybe.tmpl"

mutex_t::mutex_t() : heldby(Nothing) { pthread_mutex_init(&mux, NULL); }

mutex_t::~mutex_t() { pthread_mutex_destroy(&mux); }

mutex_t::token
mutex_t::lock() {
    fuzzsched();
    pthread_mutex_lock(&mux);
    heldby.mkjust(tid::me());
    return token(); }

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
