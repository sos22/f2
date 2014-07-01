#include "mutex.H"

mutex_t::mutex_t()
{
    pthread_mutex_init(&mux, NULL);
}

mutex_t::~mutex_t()
{
    pthread_mutex_destroy(&mux);
}

mutex_t::token
mutex_t::lock()
{
    pthread_mutex_lock(&mux);
    return token();
}

void
mutex_t::unlock(token *tok)
{
    tok->formux(*this);
    tok->release();
    pthread_mutex_unlock(&mux);
}
