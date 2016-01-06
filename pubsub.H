/* Publish/subscribe style synchronisation interface, a la
 * kernel-space waitqueues.  The idea is that anything you can wait
 * for has an associated publisher which generates events whenever it
 * changes.  Anyone who wants to wait for anything allocates a
 * subscriber structure which is used to latch events until you're
 * ready to pick them up.  You then use subscriptions to connect
 * subscribers to publishers.  Note that each event can be latched at
 * most once for each subscriber; other events after that first one
 * will be dropped.  Note also that the pubsub interface will
 * occasionally generate spurious events, which the subscriber is
 * supposed to filter out. */
/* The usual way of using this defines a bunch of predicates, defines
 * a publisher for each predicate, and arranges to notify the
 * publisher whenever the predicate changes.  Something like this:
 *
 * waiter() {
 *    subscriber sub;
 *    subscription ss(sub, pub);
 *    while (!predicate_satisfied()) {
 *        sub.wait(); } }
 *
 * waitee() {
 *     ... something which might change predicate ...;
 *     pub.publish(); }
 *
 * Note that the waiter check the predicate in between subscribing and
 * going to sleep, so as to avoid lost-wakeup races.
 *
 * Used like that, they're basically condition variables.  The big
 * advantage is that you can subscribe to several things at once and
 * wake up as soon as any get notified:
 *
 * waiter() {
 *     subscriber sub;
 *     subscription ss1(sub, pub1);
 *     subscription ss2(sub, pub2);
 *     if (predicate1()) do_something();
 *     if (predicate2()) do_something_else();
 *     while (true) {
 *         auto t = sub.wait();
 *         if (t == &ss1) do_something();
 *         else if (t == &ss2) do_something_else(); } }
 *
 * waitee1() {
 *     ... something relevant to do_something()...;
 *     pub1.publish(); }
 *
 * waitee2() {
 *     ... something relevant to do_something_else()...;
 *     pub2.publish(); }
 */
#ifndef PUBSUB_H__
#define PUBSUB_H__

#include <sys/poll.h>

#include "cond.H"
#include "fd.H"
#include "list.H"
#include "maybe.H"
#include "mutex.H"
#include "timestamp.H"

class clientio;
class publisher;
class subscriber;
class subscription;

/* Publishers represent conditions on the system state which you can
 * wait for. */
class publisher {
    friend class subscriber;
    friend class subscription;
private: mutable mutex_t mux;
private: mutable list<subscription *> subscriptions;
    /* Construct a publisher with no subscriptions */
public: publisher();
    /* Mark everything all of the subscriptions connected to this
       publisher as notified. */
public: void publish();
    /* Destruct a publisher.  It must not have any subscriptions when
     * this is called. */
public: ~publisher();
    /* Special publisher which is never notified. */
public: static const publisher dummy;
};

class subscriptionbase {
    friend class subscriber;
protected: bool notified;
protected: subscriber *sub;
public:    void *const data;
protected: subscriptionbase(subscriber &_sub, void *_data = NULL);
    /* Set the notified flag on this subscription. */
public:   void set();
    /* Called by the subscriber when it's time to unhook from the
     * publisher.  This is made public in some subclasses. */
private:   virtual void detach() = 0;
protected: virtual ~subscriptionbase();
public:    virtual const fields::field &field() const; };

/* A subscription acts to connect a subscriber to a publisher. */
class subscription final : public subscriptionbase {
    friend class publisher;
private:  const publisher *pub;
    /* Construct a new subscription connecting a given publisher to a
     * given subscriber. */
public:  subscription(subscriber &, const publisher &, void *data = NULL);
    /* Detach the subscription, disconnecting it from the publisher.
     * The subscription remains attached to the subscriber, although
     * that doesn't usually matter a great deal unless somebody calls
     * the subscriptionbase set() method directly.  The caller must
     * ensure that this does not run concurrently with the subscriber
     * destructor.*/
public:  void detach();
    /* Destruct the subscription, detaching it first if necessary. */
public:  ~subscription();
};

/* An iosubscription connects a subscriber to an FD and a poll mask.
   The subscription is notified whenever IO of the given type becomes
   possible on the FD.  Once notified, it stops listening for further
   events until rearm() is called. */
class iosubscription : public subscriptionbase {
    friend class iopollingthread;
private: const struct pollfd pfd; /* What are we listening for? */
private: bool registered; /* Are we currently registered with the poll
                             thread?  Protected by the polling thread
                             mux. */
    /* Construct a new iosubscription connecting the subscriber to a
     * given poll descriptor. */
public:  iosubscription(subscriber &, struct pollfd);
    /* IO subscriptions are by default single-shot, so that once
     * they've been notified once they won't be notified again if
     * further IO becomes possible.  Calling rearm() resets the
     * subscription so that it can be notified again.  This should
     * only be called after the subscription has been returned from
     * subscriber::wait(). */
public:  void rearm();
private: void detach() final;
public:  const fields::field &field() const;
    /* Destruct the subscription, disconnecting the subscriber from
     * the FD. */
public:  ~iosubscription();
    /* Synchronise with the polling thread following a destruction,
     * waiting for it to stop touching the file descriptor. This is
     * moderately expensive and almost always unnecessary, because the
     * usual worst case is a pointless thread wake. The exception is
     * when close()ing a file descriptor has visible side-effects,
     * because those side effects will get delayed until the polling
     * thread removes the FD from its ppoll set. Synchronising waits
     * for that to happen on all destructed iosubscriptions. */
public:  static void synchronise(clientio); };

/* A subscriber represent someone who waits for events to be
 * published.  Each subscriber can be subscribed to one or more
 * publishers and will be woken whenever any of them publish. */
class subscriber {
    friend class subscriptionbase;
    friend class subscription;
private: mutex_t mux;
private: cond_t cond;
private: unsigned nrnotified;
private: list<subscriptionbase *> subscriptions;
    /* Construct a subscriber with no subscriptions. */
public: subscriber();
    /* Wait for at least one of our subscriptions to get notified with
     * an optional timeout.  Returns the subscription which got
     * notified, or NULL if nothing was notified before the deadline.
     * The notification flag on the subscription is cleared but it
     * remains in the subscription list. */
public: subscriptionbase *wait(clientio, maybe<timestamp> deadline = Nothing);
    /* Special case of wait() with a timeout of right now, so that it
     * never waits and only returns things which have already been
     * notified. */
public: subscriptionbase *poll();
    /* Destruct a subscriber.  Any outstanding subscriptions are
     * automatically unsubscribed.  The caller must ensure that the
     * subscriber destructor does not run concurrently with the
     * destructor for any subscribed subscriptions (although running
     * them atomically in either order is safe). */
public: ~subscriber();
public: const fields::field &field() const; };

void initpubsub();
void deinitpubsub(clientio);

namespace tests {
template <typename> class event;
extern event<void> iosubdetachrace; }

#endif /* !PUBSUB_H__ */
