#include "cond.H"

cond_t::cond_t(mutex_t &_associated_mux)
    : associated_mux(_associated_mux)
{
    pthread_cond_init(&cond, NULL);
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
cond_t::wait(mutex_t::token *tok) const
{
    tok->formux(associated_mux);
    tok->release();
    pthread_cond_wait(const_cast<pthread_cond_t *>(&cond), &associated_mux.mux);
    return mutex_t::token();
}
