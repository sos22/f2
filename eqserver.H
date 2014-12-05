/* Server side of an event queue.  These generate events in a way
 * which can be nicely pushed to clients. */
#ifndef EQSERVER_H__
#define EQSERVER_H__

#include "eq.H"
#include "list.H"
#include "maybe.H"
#include "rpcservice2.H"
#include "serialise.H"

class eqserver;

/* Bits of the event queue which are common to all event types.  You
 * basically never want to instantiate this directly; the
 * eqserver/geneqserver split is just an implementation detail. */
class geneventqueue {
    friend class eqserver;
private:   geneventqueue() = delete;
private:   geneventqueue(const geneventqueue &) = delete;
private:   void operator=(const geneventqueue &) = delete;
    /* Would be private if I knew how to do the friend
     * declarations. */
public:    class impl;
    friend class impl;
public:    impl &implementation();

public:    const proto::eq::genname name;

private:   explicit geneventqueue(const proto::eq::genname &);
private:   static nnp<geneventqueue> build(const proto::eq::genname &);

private:   class event;

    /* Bits of state needed to queue a message without needing lambdas
     * or virtual methods. */
protected: class queuectxt {
    public: list<event>::partial *inner;
    public: maybe<serialise1> serialiser;
    public: explicit queuectxt(geneventqueue &);
        /* Used once we've finished serialising the message to
         * actually add it to the queue.  Must be invoked before the
         * context is destructed. */
    public: void finish(geneventqueue &, rpcservice2::acquirestxlock); };
    friend class queuectxt;

protected: void destroy(rpcservice2::acquirestxlock);
protected: ~geneventqueue(); };

/* Event queue parameterised on the type of event (which must support
 * the serialise/deserialise protocol).  Most of the actual work is
 * done in geneventqueue. */
template <typename t> class eventqueue final : private geneventqueue {
    /* It's important that this has no state of its own, because its
     * destructor will never actually get invoked. */

private: eventqueue() = delete;
private: eventqueue(const eventqueue &) = delete;
private: void operator=(const eventqueue &) = delete;

    /* Add an event to the end of the queue.  Note that this is
     * asynchronous: it returns as soon as the event is added to the
     * queue, even if some clients have not received it. */
public:  void queue(const t &what, rpcservice2::acquirestxlock atl) {
        queuectxt ctxt(*this);
        if (ctxt.serialiser.isnothing()) return;
        ctxt.serialiser.just().push(what);
        ctxt.finish(*this, atl); }

    /* Unhook an event queue from the server, if it hasn't already
     * been unhooked, and release it. */
public:  void destroy(rpcservice2::acquirestxlock atl) {
        geneventqueue::destroy(atl); }

    /* Never actually invoked, because we're always destroyed by the
     * geneventqueue destroy method and geneventqueue's destructor is
     * non-virtual.  */
private: ~eventqueue() = delete; };

/* A thing for managing a group of event queues and exposing them
 * easily over an rpcservice. */
class eqserver {
private: eqserver() {}
private: eqserver(const eqserver &) = delete;
private: void operator=(const eqserver &) = delete;
    /* Should be private, but I can't see how to write the necessary
     * friend declarations. */
public:  class impl;
    friend class impl;
public:  impl &implementation();

private: void registerqueue(geneventqueue &);

    /* Allocate a new eqservice and return it.  The service initially
     * has no queues. */
public:  static nnp<eqserver> build();

    /* Interface to rpcservice.  All messages received by the service
     * on the eq interfacetype should be passed straight to this
     * function. */
public:  orerror<void> called(clientio io,
                              deserialise1 &ds,
                              nnp<rpcservice2::incompletecall> ic,
                              rpcservice2::onconnectionthread oct);

    /* Create a new event queue with a particular type and name and
     * return it.  The queue is initially empty.  It is accessible to
     * clients over the RPC interface as soon as this returns. */
public:  template <typename t> nnp<eventqueue<t> > mkqueue(
    proto::eq::name<t> name) {
        auto n(geneventqueue::build(name));
        registerqueue(*n);
        return _nnp(*(eventqueue<t> *)&*n); }

    /* Tear down the server.  Obviously, once this is called it is no
     * longer possible to call called(), so the queues become
     * inaccessible to remote peers, but you still have to call
     * destroy() on all of them.  Adding events to the surviving
     * queues won't do any good (no client will ever receive them) but
     * is guaranteed not to crash, either. */
public:  void destroy();

    /* Use destroy() instead. */
private: ~eqserver(); };

#endif /* !EQSERVER_H__ */
