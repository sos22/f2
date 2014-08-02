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
    cond_t(mutex_t &_associated_mux);
    ~cond_t();
    void broadcast(mutex_t::token);
    /* waiting on a cond var doesn't change the cond var's state, so
       it gets a const modifier. */
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
