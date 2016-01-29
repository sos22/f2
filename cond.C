#include "cond.H"

#include <assert.h>
#include <errno.h>

#include <valgrind/valgrind.h>

#include "clientio.H"
#include "logging.H"
#include "timedelta.H"
#include "timestamp.H"

#include "fields.tmpl"
#include "maybe.tmpl"

cond_t::cond_t(mutex_t &_associated_mux)
    : associated_mux(_associated_mux),
      cond()
{
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&cond, &attr);
    pthread_condattr_destroy(&attr);
}

cond_t::~cond_t()
{
    pthread_cond_destroy(&cond);
}

void
cond_t::broadcast(mutex_t::token tok)
{
    tok.formux(associated_mux);
    pthread_cond_broadcast(&cond);
}

mutex_t::token
cond_t::wait(clientio, mutex_t::token *tok) const
{
    tok->formux(associated_mux);
    tok->release();
    assert(associated_mux.heldby.load() == tid::me().os());
    associated_mux.heldby.store(0);
    pthread_cond_wait(const_cast<pthread_cond_t *>(&cond), &associated_mux.mux);
    assert(associated_mux.heldby.load() == 0);
    associated_mux.heldby.store(tid::me().os());
    return mutex_t::token();
}

cond_t::waitres
cond_t::wait(clientio io,
             mutex_t::token *tok,
             maybe<timestamp> _deadline) const {
    if (_deadline == Nothing) {
        waitres res;
        res.timedout = false;
        res.token = wait(io, tok);
        return res; }
    auto deadline(_deadline.just());
    bool warped = RUNNING_ON_VALGRIND;

  retry:
    /* If we're running on Valgrind then we need to adjust for the
     * timewarp. Basically, we want to wake up immediately after
     * timestamp::now() reaches the deadline. timestamp::now()
     * advances at a twentieth the speed of the kernel clock. */
    auto n(timestamp::now());
    auto dd(deadline - timestamp::now());
    if (dd > 86400_s) logmsg(loglevel::error, "long timeout "+deadline.field());
    if (warped) deadline = n + (deadline - n) * VALGRIND_TIMEWARP;
    tok->formux(associated_mux);
    tok->release();
    assert(associated_mux.heldby.load() == tid::me().os());
    associated_mux.heldby.store(0);
    struct timespec ts(deadline.as_timespec());
    int r = pthread_cond_timedwait(const_cast<pthread_cond_t *>(&cond),
                                   &associated_mux.mux,
                                   &ts);
    assert(associated_mux.heldby.load() == 0);
    associated_mux.heldby.store(tid::me().os());
    assert(r == 0 || r == ETIMEDOUT);
    waitres res;
    res.token = mutex_t::token();
    res.timedout = r == ETIMEDOUT;
    if (res.timedout && warped && deadline.infuture()) {
        /* The interval between un-warping the deadline and actually
         * going to sleep isn't un-warped, which can sometimes lead to
         * us timing out before the deadline has actually
         * happened. Just retry if that happens. */
        deadline = _deadline.just();
        goto retry; }
    return res; }
