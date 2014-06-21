/* Publish/subscribe style synchronisation interface, a la
 * kernel-space waitqueues. */
#ifndef PUBSUB_H__
#define PUBSUB_H__

#include "cond.H"
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

/* A subscription acts to connect a subscriber to a publisher. */
class subscription {
    friend class subscriber;
    friend class publisher;
private:  bool notified;
private:  subscriber *sub;
private:  publisher *pub;
    /* Construct a new subscription connecting a given publisher to a
     * given subscriber. */
public:  subscription(subscriber &, publisher &);
    /* Set the notified flag on this subscription.  Only called by
     * publisher. */
private: void set();
    /* Destruct the subscription, disconnecting from the publisher and
     * the subscriber.  The caller must ensure that this does not run
     * concurrently with the subscriber destructor.*/
public:  ~subscription();
};

/* A subscriber represent someone who waits for events to be
 * published.  Each subscriber can be subscribed to one or more
 * publishers and will be woken whenever any of them publish. */
class subscriber {
    friend class subscription;
private: mutex_t mux;
private: cond_t cond;
private: bool notified;
private: list<subscription *> subscriptions;
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
public: subscription *wait(maybe<timestamp> deadline = Nothing);
    /* Destruct a subscriber.  Any outstanding subscriptions are
     * automatically unsubscribed, leaving the subscription in an
     * invalid state in which the only safe operation is to destruct
     * it.  The caller must ensure that the subscriber destructor does
     * not run concurrently with the destructor for any subscribed
     * subscriptions (although running them atomically in either order
     * is safe). */
public: ~subscriber();
};

class test;
namespace tests {
void pubsub(test &);
}

#endif /* !PUBSUB_H__ */
