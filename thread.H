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
public:
    void join();
    static maybe<error> spawn(threadfn *, thread **out);
};

#endif
