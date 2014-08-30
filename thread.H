#ifndef THREAD_H__
#define THREAD_H__

#include <pthread.h>

#include "clientio.H"
#include "pubsub.H"
#include "tid.H"
#include "waitbox.H"

namespace fields {
class field;
}

class thread;

namespace fields {
const field &mk(const thread &);
}

/* Threads are initially creted in a paused state, allowing some
   initialisation work before the thread's run() method is called. */
template <typename t> class pausedthread {
    friend class thread;
private: pausedthread() = delete;
private: pausedthread(t *);
private: t *inner_;
    /* Unpause the thread and let it go.  The pausedthread structure
       becomes invalid when this is called. */
public:  t *go();
    /* Paused threads can be destroyed without needing a clientio
       token.  Destroyed threads should not be unpaused. */
public:  void destroy();
    /* Extract the wrapper thread structure, to allow any necessary
     * initialisation to be run. */
public:  t *unwrap() const {
    assert(inner_ != NULL);
    return inner_; }
};

class thread {
    template <typename t> friend class pausedthread;
public:    class deathsubscription;
    friend class deathsubscription;
    friend const fields::field &fields::mk(const thread &);
private:   pthread_t thr;
private:   bool started;
private:   waitbox<tid> tid_;
private:   const char *name; /* nul-terminated malloc()ed owned */
private:   bool dead; /* Access with loadacquire() and storerelease() */
private:   list<deathsubscription *> subscribers;
protected: class constoken {
    friend class thread;
    private: const fields::field &name;
    private: constoken(const fields::field &_name)
        : name(_name) {}
    };
protected: thread(constoken);
    /* Create a new thread.  Template parameter t should be a derived
       class of thread.  args are arguments to its constructor.  The
       new thread is initially paused and will become runnable when
       go() is called.  The constructor should only block for a long
       time if the argument list includes a clientio token.  The name
       gives a name for the thread which will be made available in the
       debugger and through the fields::mk() method.  The derived
       class should usually have only private constructors and should
       be friends with this one. */
    /* Note that this will abort() if the thread creation fails. */
public:    template <typename t, typename ... args>
    static pausedthread<t> spawn(
    const fields::field &name,
    args &&...params);
    /* Slightly more intricate version of spawn() which allows callers
       to specify their own token type and a mechanism for lifting
       thread constructor tokens into their token type. */
public:    template <typename t, typename token, typename ... args>
    static pausedthread<t> _spawn(
        const fields::field &name,
        const std::function<token (const constoken &)> &wraptoken,
        args &&... params);
private:   void go();
private:   static void *pthreadstart(void *);
    /* The main thread run method.  Derived classes should override
     * this to control what the thread does.  Note that the thread
     * structure remains valid after this returns until somebody calls
     * join(). */
private:   virtual void run(clientio) = 0;
    /* A deathtoken is proof that a particular thread has exitted and
     * can be presented to join() in lieu of a clientio token, because
     * once the thread has died join() is guaranteed to be quick. */
public:    class deathtoken {
        friend class thread;
    private: deathtoken() {}
    };
    /* Check whether the thread has exitted.  Returns a death token if
     * it has or Nothing if it's still running. */
public:    maybe<deathtoken> hasdied() const;
    /* Mechanism for waiting for a thread to die.  A death
     * subscription will be notified whenever hasdied() goes from
     * Nothing to a valid death token.  It will also automatically be
     * notified when added to a subscriber if the thread is already
     * dead. */
public:    class deathsubscription : public subscriptionbase {
        friend class thread;
    private: deathsubscription() = delete;
    private: deathsubscription(const deathsubscription &) = delete;
    private: void operator =(const deathsubscription &) = delete;
    private: thread *owner;
        /* Construct a new death subscription for a given thread.  The
         * subscription will be notified shortly after the thread
         * hasdied() method starts returning a valid death token, and
         * also if it is attached to a subscriber while the hasdied()
         * method will return a valid token. */
        /* Note that this is still edge, rather than level, sensitive:
         * each subscription will only be notified once when the
         * thread dies. */
    public:  deathsubscription(subscriber &sub, thread *owner);
        /* Detach the thread death subscription from the thread.  The
         * subscription will no longer be notified when the thread
         * dies.  It is valid to call detach() multiple times (but
         * pointless). */
    public:  void detach();
        /* Default destructor.  Will detach() first if necessary. */
    public:  ~deathsubscription();
    };
    /* Clean up a thread structure after the thread has died.  Note
     * that this deletes this.  Automatically detaches any remaining
     * death subscriptions. */
public:     void join(deathtoken);
    /* Convenience function which waits for a thread to die and then
     * join()s it. */
public:     void join(clientio);
    /* Should be private, but gcc doesn't let you inherit from classes
     * with a private destructor.  Can be overridden by child
     * classes. */
protected: virtual ~thread();
};

namespace tests {
void thread();
}

#endif /* !THREAD_H__ */
