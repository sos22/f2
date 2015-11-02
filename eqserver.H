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

class eventqueueconfig {
private: eventqueueconfig();
    /* Maximum number of events in a queue.  We drop anything past
     * this limit, even if some clients still need them. */
public:  unsigned queuelimit;
    /* How many event IDs should be allocated at a time? */
public:  unsigned idallocsize;
    /* Generate an eqserverconfig with reasonable defaults. */
public:  static eventqueueconfig dflt();
    /* Parse an event queue config from a serialise dump. */
public:  explicit eventqueueconfig(deserialise1 &);
    /* Serialise an event queue config. */
public:  void serialise(serialise1 &) const;
};

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
public:    const impl &implementation() const;

public:    const proto::eq::genname name;

private:   geneventqueue(const proto::eq::genname &);
private:   static orerror<void> formatqueue(const proto::eq::genname &,
                                            const filename &,
                                            const eventqueueconfig &);
private:   static orerror<nnp<geneventqueue> > openqueue(
    const proto::eq::genname &,
    const filename &);

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
    public: proto::eq::eventid finish(geneventqueue &,
                                      rpcservice2::acquirestxlock); };
    friend class queuectxt;

protected: proto::eq::eventid lastid() const;
protected: proto::eq::eventid allocdummyid();
protected: void destroy(rpcservice2::acquirestxlock);
protected: ~geneventqueue();
public:    static tests::hookpoint<void> finishingpush; };

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
     * queue, even if some clients have not received it.  Returns the
     * event ID of the event within the queue. */
public:  proto::eq::eventid queue(const t &what,
                                  rpcservice2::acquirestxlock atl) {
        queuectxt ctxt(*this);
        if (ctxt.serialiser.isnothing()) return geneventqueue::allocdummyid();
        ctxt.serialiser.just().push(what);
        return ctxt.finish(*this, atl); }

    /* Find the event ID assigned to the last event to be added to the
     * queue.  This has obvious synchronisation issues if several
     * threads add events to the same queue; just be careful. */
public:  proto::eq::eventid lastid() const { return geneventqueue::lastid(); }

    /* Unhook an event queue from the server, if it hasn't already
     * been unhooked, and release the in-memory state.  The on-disk
     * state is untouched and can later be re-opened. */
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

    /* Create a new queue, given its name and configuration and a file
     * in which to store the necessary persistent state.  The state
     * file is specific to the given queue name; it is not valid to
     * use the same state file for a different queue name, The state
     * file contains, amongst other information, the queue
     * configuration, which cannot be changed once the queue has been
     * created. */
    /* You might think that this would make more sense in the
     * eventqueue class.  You'd be right, except that putting it here
     * plays better with C++'s template inference algorithm. */
public:  template <typename t> static orerror<void> formatqueue(
    const proto::eq::name<t> &name,
    const filename &state,
    const eventqueueconfig &config = eventqueueconfig::dflt()) {
        return geneventqueue::formatqueue(name, state, config); }

    /* Open a queue previously created with eventqueue::formatqueue().
     * The queue is accessible over the RPC interface as soon as this
     * returns.  Closing and re-opening a queue may or may not cause
     * the events in it to be dropped; if they are dropped, we will
     * arrange to send appropriate queue-dropped notifications to the
     * clients. */
public:  template <typename t> orerror<nnp<eventqueue<t> > > openqueue(
    proto::eq::name<t> name,
    const filename &state) {
        auto n(geneventqueue::openqueue(name, state));
        if (n.isfailure()) return n.failure();
        registerqueue(*n.success());
        return _nnp(*(eventqueue<t> *)&*n.success()); }

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
