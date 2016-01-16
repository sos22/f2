#include "cond.H"

#include <assert.h>
#include <errno.h>

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
    assert(associated_mux.heldby == tid::me().os());
    associated_mux.heldby = 0;
    pthread_cond_wait(const_cast<pthread_cond_t *>(&cond), &associated_mux.mux);
    assert(associated_mux.heldby == 0);
    associated_mux.heldby = tid::me().os();
    return mutex_t::token();
}

cond_t::waitres
cond_t::wait(clientio io,
             mutex_t::token *tok,
             maybe<timestamp> deadline) const {
    if (deadline == Nothing) {
        waitres res;
        res.timedout = false;
        res.token = wait(io, tok);
        return res; }
    
    auto dd(deadline.just() - timestamp::now());
    if (dd > 86400_s) logmsg(loglevel::error, "long timeout "+deadline.field());
    tok->formux(associated_mux);
    tok->release();
    struct timespec ts(deadline.just().as_timespec());
    assert(associated_mux.heldby == tid::me().os());
    associated_mux.heldby = 0;
    int r = pthread_cond_timedwait(const_cast<pthread_cond_t *>(&cond),
                                   &associated_mux.mux,
                                   &ts);
    assert(associated_mux.heldby == 0);
    associated_mux.heldby = tid::me().os();
    assert(r == 0 || r == ETIMEDOUT);
    waitres res;
    res.token = mutex_t::token();
    res.timedout = r == ETIMEDOUT;
    return res; }
