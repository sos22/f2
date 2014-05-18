#include "cond.H"

mutex_t::token
cond_t::wait(mutex_t::token *tok)
{
    pthread_cond_wait(&cond, &mux->mux);
    return *tok;
}

cond_t::cond_t(mutex_t *_mux)
    : mux(_mux)
{
    pthread_cond_init(&cond, NULL);
}

cond_t::~cond_t()
{
    pthread_cond_destroy(&cond);
}
