#ifndef THREAD_H__
#define THREAD_H__

#include <pthread.h>
#include <functional>

#include "cond.H"
#include "maybe.H"
#include "pubsub.H"
#include "tid.H"
#include "waitbox.H"

class clientio;
class error;
class mutex_t;
class thread;

namespace fields {
class field;
};

class threadfn {
public:
    virtual void run(clientio) = 0;
    virtual ~threadfn() {}
};

template <typename t>
class sparkbox : public waitbox<t> {
private: thread *thr;
public:  ~sparkbox();
};

class thread {
    friend class threadfield;
    pthread_t thr;
    threadfn *func;

    maybe<tid> tid_;
    mutex_t startmux;
    cond_t startcond;
    publisher pub_;
    bool finished_;

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
    void join(clientio);
    /* Check whether the thread has exitted yet.  Note that this might
     * return false for a little while after the thread function
     * returns.  That lag will always be short and finite.  The
     * publisher will be notified when this becomes true. */
    bool finished() const;
    /* Publisher which publishes when the thread exits (or, more
     * precisely, shortly after finished() becomes true). */
    const publisher &pub;
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
