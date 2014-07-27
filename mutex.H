#ifndef MUTEX_H__
#define MUTEX_H__

#include <pthread.h>

class cond_t;

class mutex_t {
    friend class cond_t;
    pthread_mutex_t mux;
public:
    /* lock acquisition returns a token, for a couple of reasons:

       1) It makes it explicit which unlock() goes with which lock(),
       which is good documentation and also handy for tracking down
       missing unlock() errors.
       2) It allows us to document that a particular function either
       requires a lock to be held on entry (by taking a token as
       an argument) or acquires the lock (by returning one) or releases
       a lock (by taking the lock as a pointer argument).
       3) It allows the compiler to check that we're at least vaguely
       in agreement with that documentation.
    */
    class token {
        token() {}
        friend class mutex_t;
        friend class cond_t;
        void release() {}
    public:
        /* Assert/document that a token must have been obtained from a
           particular mutex.  Doesn't actually do anything in this
           implementation. */
        void formux(mutex_t &) {}
    };
    mutex_t();
    ~mutex_t();
    token lock();
    void unlock(token *);
};

namespace tests {
void mutex(void);
}

#endif
