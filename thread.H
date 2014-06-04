#ifndef THREAD_H__
#define THREAD_H__

#include <pthread.h>
#include "error.H"
#include "maybe.H"

class threadfn {
public:
    virtual void run() = 0;
};

class thread {
    pthread_t thr;
    threadfn *func;
    static void *startfn(void *_this);
    thread() {}
    thread(const thread &o) = delete;
    void operator=(const thread &o) = delete;
public:
    /* Construct and start a new thread.  @out is set to the new
       thread structure, before the thread starts running, avoiding
       the usual thread startup races. */
    static maybe<error> spawn(threadfn *, thread **out);
    /* Wait for the thread to finish and tear down the thread
     * structure. */
    void join();
};

#endif
