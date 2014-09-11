#ifndef COND_H__
#define COND_H__

#include <pthread.h>

#include "mutex.H"

class clientio;
template <typename> class maybe;
class timestamp;

class cond_t {
    mutex_t &associated_mux;
    pthread_cond_t cond;
    cond_t() = delete;
    cond_t(const cond_t &) = delete;
    void operator=(const cond_t &) = delete;
public:
    /* In our model, each cond_t has an associated mutex which must be
     * held in order to wait on it and which will be dropped for the
     * duration of the wait.  This is in contrast to pthread-style
     * condvars which can be mixed and matched arbitrarily with
     * whatever mutexes are convenient throughout their life. */
    explicit cond_t(mutex_t &_associated_mux);
    ~cond_t();
    /* Unlike a normal pthread_cond_t, our cond variables can only be
       broadcast(), not signal()led, and can only be broadcast() when
       you hold the relevant mutex.  There's no fundamental reason for
       that, except for, to my mind, making things easier to
       understand. */
    void broadcast(mutex_t::token);
    /* waiting on a cond var doesn't change the cond var's state, so
       it gets a const modifier.  Unlike a pthread condvar, our wait
       operation can be spuriously woken at any point (although for
       performance reasons such wakes will be rare). */
    mutex_t::token wait(clientio, mutex_t::token *) const;
    /* Wait on a condvar until some timeout. */
    struct waitres {
        bool timedout;
        mutex_t::token token;
    };
    waitres wait(clientio, mutex_t::token *, maybe<timestamp> deadline) const;

};

namespace tests {
void cond();
}

#endif /* !COND_H__ */
