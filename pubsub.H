/* Publish/subscribe style synchronisation interface, a la
 * kernel-space waitqueues. */
#ifndef PUBSUB_H__
#define PUBSUB_H__

#include "cond.H"
#include "fd.H"
#include "list.H"
#include "maybe.H"
#include "mutex.H"
#include "timestamp.H"

class publisher;
class subscriber;
class subscription;

/* Publishers represent conditions on the system state which you can
 * wait for. */
class publisher {
    friend class subscriber;
    friend class subscription;
private: mutex_t mux;
private: list<subscription *> subscriptions;
    /* Construct a publisher with no subscriptions */
public: publisher();
    /* Mark everything all of the subscriptions connected to this
       publisher as notified. */
public: void publish();
    /* Destruct a publisher.  It must not have any subscriptions when
     * this is called. */
public: ~publisher();
};

class subscriptionbase {
    friend class subscriber;
    friend class publisher;
protected: bool notified;
protected: subscriber *sub;
protected: subscriptionbase(subscriber &_sub);
    /* Set the notified flag on this subscription.  Only called by
     * publisher and derived classes. */
protected: void set();
    /* Called by the subscriber when it's time to unhook from the
     * publisher. */
private:   virtual void teardown() = 0;
protected: ~subscriptionbase();
};

/* A subscription acts to connect a subscriber to a publisher. */
class subscription : public subscriptionbase {
    friend class publisher;
private:  publisher *pub;
    /* Construct a new subscription connecting a given publisher to a
     * given subscriber. */
public:  subscription(subscriber &, publisher &);
private: void teardown();
    /* Destruct the subscription, disconnecting from the publisher and
     * the subscriber.  The caller must ensure that this does not run
     * concurrently with the subscriber destructor.*/
public:  ~subscription();
};

/* An iosubscription connects a subscriber to an FD and a poll mask.
   The subscription is notified whenever IO of the given type becomes
   possible on the FD.  Once notified, it stops listening for further
   events until rearm() is called. */
class iosubscription : public subscriptionbase {
    friend class iopollingthread;
private: fd_t fd; /* Which FD are we listening to? */
private: int events; /* Which events are we interested in? */
private: bool registered; /* Are we currently registered with the poll
                             thread?  Protected by the polling thread
                             mux. */
    /* Construct a new iosubscription connecting the subscriber to the
     * file descriptor. */
public:  iosubscription(subscriber &, fd_t fd, int pollmask);
    /* IO subscriptions are by default single-shot, so that once
     * they've been notified once they won't be notified again if
     * further IO becomes possible.  Calling rearm() resets the
     * subscription so that it can be notified again.  This should
     * only be called after the subscription has been returned from
     * subscriber::wait(). */
public:  void rearm();
private: void teardown();
    /* Destruct the subscription, disconnecting the subscriber from
     * the FD. */
public:  ~iosubscription();
};

/* A subscriber represent someone who waits for events to be
 * published.  Each subscriber can be subscribed to one or more
 * publishers and will be woken whenever any of them publish. */
class subscriber {
    friend class subscriptionbase;
    friend class subscription;
private: mutex_t mux;
private: cond_t cond;
private: bool notified;
private: list<subscriptionbase *> subscriptions;
    /* One of the subscriptions in the list was notified.  Update the
       subscriber, unblocking if necessary.  Caller must have acquired
       the subscriber lock. */
private: void set(mutex_t::token);
    /* Construct a subscriber with no subscriptions. */
public: subscriber();
    /* Wait for at least one of our subscriptions to get notified with
     * an optional timeout.  Returns the subscription which got
     * notified, or NULL if nothing was notified before the deadline.
     * The notification flag on the subscription is cleared but it
     * remains in the subscription list. */
public: subscriptionbase *wait(maybe<timestamp> deadline = Nothing);
    /* Destruct a subscriber.  Any outstanding subscriptions are
     * automatically unsubscribed, leaving the subscription in an
     * invalid state in which the only safe operation is to destruct
     * it.  The caller must ensure that the subscriber destructor does
     * not run concurrently with the destructor for any subscribed
     * subscriptions (although running them atomically in either order
     * is safe). */
public: ~subscriber();
};

void initpubsub();
void deinitpubsub();

class test;
namespace tests {
void pubsub(test &);
}

#endif /* !PUBSUB_H__ */
