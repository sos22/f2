#ifndef MUTEX_H__
#define MUTEX_H__

#include <pthread.h>

#include <functional>

#include "maybe.H"
#include "tid.H"

class cond_t;

class mutex_t {
    friend class cond_t;
    pthread_mutex_t mux;
    maybe<tid> heldby;
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
        ~token() {};
        token(const token &) {}
        void operator=(const token &) {}
    };
    mutex_t();
    ~mutex_t();
    token lock();
    template <typename t> t locked(const std::function<t (token)> &);
    template <typename t> t locked(const std::function<t (void)> &);
    void locked(const std::function<void (token)> &);
    void locked(const std::function<void (void)> &);

    /* Generate a dummy token without acquiring the lock.  Useful if
     * we know that the lock has somehow become private to the calling
     * thread. */
    token DUMMY();

    void unlock(token *);
};

namespace tests {
void mutex(void);
}

#endif
