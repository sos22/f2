#ifndef THREAD_H__
#define THREAD_H__

#include <pthread.h>
#include "cond.H"
#include "maybe.H"
#include "tid.H"

class error;
class mutex_t;

namespace fields {
class field;
};

class threadfn {
public:
    virtual void run() = 0;
    virtual ~threadfn() {}
};

class thread {
    friend class threadfield;
    pthread_t thr;
    threadfn *func;

    maybe<tid> tid_;
    mutex_t startmux;
    cond_t startcond;

    const char *name;
    static void *startfn(void *_this);
    thread();
    thread(const thread &o) = delete;
    void operator=(const thread &o) = delete;
public:
    /* Construct and start a new thread.  @out is set to the new
       thread structure, before the thread starts running, avoiding
       the usual thread startup races. */
    static maybe<error> spawn(threadfn *,
                              thread **out,
                              const fields::field &name);
    /* Wait for the thread to finish and tear down the thread
     * structure. */
    void join();
};

class threaddestructor {
    friend class thread;
private:   mutex_t mux;
private:   cond_t idle;
private:   threaddestructor *next;
private:   unsigned outstanding;
private:   void start();
private:   void finished();
protected: threaddestructor();
protected: ~threaddestructor();
private:   virtual void die() = 0;
};

namespace fields {
const field &mk(const thread &t);
};

#endif
