#ifndef COND_H__
#define COND_H__

#include "mutex.H"
class cond_t {
    mutex_t *mux;
    pthread_cond_t cond;
public:
    cond_t(mutex_t *);
    ~cond_t();
    mutex_t::token wait(mutex_t::token *tok);
};

#endif
