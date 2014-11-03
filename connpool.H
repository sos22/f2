/* Wrapper around rpcclient and beaconclient which allows callers to
 * connect by slave name, rather than by peer name, and which handles
 * reconnecting if we lose contact with a slave (and retrying
 * outstanding calls). */
#ifndef CONNPOOL_H__
#define CONNPOOL_H__

#include "probability.H"
#include "rpcclient.H"
#include "slavename.H"
#include "timedelta.H"
#include "tuple.H"
#include "waitbox.H"

class beaconclient;
class connpool;

#define _connpoolconfig(ctxt, iter0, iter1)                             \
    /* How should we connect to the other side? */                      \
    iter0(ctxt, 1, mand, ref, ::rpcclientconfig, clientconfig)          \
    /* How long should we wait trying to connect to remote peers? */    \
    iter0(ctxt, 2, mand, value, ::timedelta, connecttimeout)            \
    /* How many attempts should we make on each call before giving */   \
    /* up and returning an error? */                                    \
    iter0(ctxt, 3, mand, value, unsigned, callretries)                  \
    /* How long should we cache connections which aren't being used? */ \
    iter0(ctxt, 4, mand, value, ::timedelta, expirytime)                \
    /* Debug mode: randomly duplicate some proportion of calls. */      \
    /* Disconnect recovery can sometimes duplicate requests, and */     \
    /* this makes it a bit easier to test the dupe suppression */       \
    /* special cases. */                                                \
    iter1(ctxt, 5, mand, value, ::probability, dupecalls)
class connpoolconfig {
    mktuplefields(connpoolconfig, public)
public: static connpoolconfig dflt(); };
mktupleext(connpoolconfig);

#define _connpoolstatus(ctxt, iter0, iter1)             \
    iter1(ctxt, 1, mand, ref, ::connpoolconfig, config)
class connpoolstatus {
    mktuplefields(connpoolstatus, public); };
mktupleext(connpoolstatus);

class connpoolmaintenance;

/* Thin wrapper around rpcclient. */
class pooledconnection {
    friend class connpool;
    friend class connpoolmaintenance;
public:  class asynccall;
private: pooledconnection() = delete;
private: pooledconnection(const pooledconnection &) = delete;
private: void operator=(const pooledconnection &) = delete;

    /* Protects all non-const fields.  Above the per-call lock in the
     * lock hierarchy, below the pool lock. */
private: mutex_t mux;
    /* Who are we connected to (or trying to connect to)? */
private: const slavename name;
    /* Connection pool which the connection came from, or NULL if the
     * connpool has been torn down.  Only changed by maintenance
     * thread. */
private: connpool *owner;
    /* Simple reference count.  The connection can be safely torn down
     * once this reaches zero.  Only incremented if we're still in the
     * pool list.  Outstanding calls hold a reference. */
private: unsigned refcount;
    /* The time at which refcount reached zero, or Nothing if refcount
     * is currently non-zero. */
private: maybe<timestamp> idledat;
    /* All of the calls which are currently outstanding on this
     * connection. */
private: list<asynccall *> outstanding;
    /* The client connection.  Nothing if we're still waiting for the
     * beacon, left once we've got a beacon result and started the
     * rpcclient connect, and right once the rpcclient connect is
     * finished. */
private: maybe<either<rpcclient::asyncconnect *, rpcclient *> > inner;
    /* Subscription connecting the maintenance thread to the inner
     * asyncconnect, if we have one.  Subscription data is this with
     * the low bit set. */
private: maybe<subscription> connectsub;
    /* When did we start trying to connect this connection?  Nothing
     * if we're not currently trying to connect. */
private: maybe<timestamp> connectstart;
    /* Set if the connection has encountered an error which requires
     * it to be shut down.  Only accessed by maintenance thread. */
private: bool errored;
    /* Published whenever a new call arrives in the outstanding
     * list or when a call gets abort()ed. */
private: publisher newcall;
    /* Conects the maintenance thread to newcalls.  Subscription data
     * is this with the low two bits set.  Nothing once the
     * maintenance thread has been torn down and before the
     * maintenance thread finds out about this conn. */
private: maybe<subscription> newcallsub;

    /* Connects the maintenance thread to inner asyncconnect, if we
     * have one. */
    /* Extract from connpool, rather than building your own. */
private: pooledconnection(connpool *_owner,
                          const slavename &_name);

    /* Reset inner to Nothing, tearing down any existing connection. */
private: void disconnect(mutex_t::token /* conn lock */);

    /* Make a call on a connection, waiting until a response is
     * received, up to a timeout.  The call abort()s if the timeout is
     * reached (which, as usual, might happen after the remote peer
     * has started processing the call).  Handles retrying the call if
     * the peer crashes (up to a limit defined in the pool config). */
public:  orerror<wireproto::rx_message *> call(
    clientio,
    const wireproto::req_message &,
    maybe<timestamp> deadline = Nothing);

    /* Asynchronous version of the call interface.  This is very
     * similar to the matching rpcclient interface, except for
     * providing failover if the peer crashes halfway through the
     * call.  Any calls still outstanding when the connpool is
     * destroyed will return error::disconnected. */
public:  class asynccall {
        friend class pooledconnection;
        friend class connpoolmaintenance;
    private: asynccall(const asynccall &) = delete;
    private: void operator=(const asynccall &) = delete;
        /* Protects all of our fields.  Leaf lock.  mutable so that we
         * can acquire it from finished(). */
    private: mutable mutex_t mux;
        /* The pooledconnection we should be talking to, or NULL if
         * it's been torn down.  Only ever modified by the maintenance
         * thread. */
    private: pooledconnection *owner;
        /* Keep a copy of the request message, in case we need to
         * retransmit.  Can be NULL. */
    private: wireproto::req_message *const msg;
        /* How many times have we tried the call so far? */
    private: unsigned nrretries;
        /* Current rpcclient asynccall, if we have one.  Only accessed
         * by maintenance thread. */
    private: rpcclient::asynccall *inner;
        /* Result of the call, if we have it. */
    private: maybe<orerror<wireproto::rx_message *> > res;
        /* Subscription connecting the worker thread to inner, or
         * Nothing if inner is NULL. */
    private: maybe<subscription> sub;
        /* Notified whenever finished() goes non-Nothing i.e. if
         * nrretries reaches the limit or res becomes non-Nothing. */
    private: publisher _pub;
        /* Set when abort() gets called.  Once this is set the
         * maintenance thread can delete the call at any time. */
    private: bool aborted;
        /* Normal asynccall structure. */
    private: explicit asynccall(pooledconnection *,
                                const wireproto::req_message &msg);
        /* asynccall started after the connpool has been torn down.
         * Immediately returns error::disconnected. */
    private: asynccall();
        /* Call finished token.  Only constructed once the call has
         * finished. */
    public:  class token {
            friend class asynccall;
        private: token(); };
        /* Check whether the call has finished.  Return a finished
         * token if it has or Nothing otherwise. */
    public:  maybe<token> finished() const;
        /* Publisher which is set when finished() goes from Nothing to
         * something. */
    public:  const publisher &pub() const;
        /* Extract the result of the call, destroying it in the
         * process.  Only valid once finished() returns
         * non-Nothing. */
    public:  orerror<wireproto::rx_message *> pop(token);
        /* Abandon a call.  There is no way to tell whether the call
         * completed before it was abort()ed.  The asynccall structure
         * is destroyed. */
    public:  void abort();
        /* Convenience function: wait for finished() to go non-Nothing
         * and then call pop(), with an optional deadline. */
    public:  orerror<wireproto::rx_message *> pop(clientio,
                                                  maybe<timestamp> = Nothing);
        /* Call abort() or pop() instead. */
    private: ~asynccall(); };
public:  asynccall *call(const wireproto::req_message &);

    /* Release a reference to the pooled connection, allowing the
     * connpool to tear it down, if it wants to.  No further calls can
     * be started but existing calls will complete normally, unless
     * the connpool itself is also torn down. */
public:  void put();

    /* Use put() */
private: ~pooledconnection(); };

class connpool {
    friend class pooledconnection;
    friend class connpoolmaintenance;

private: void operator=(const connpool &) = delete;
private: connpool() = delete;
private: connpool(const connpool &) = delete;

    /* Protects all of our non-const fields.  Ordered above the conn
     * and call locks. */
private: mutex_t mux;
    /* Published whenever a connection is idled, errored, or added
     * to the connections list. */
private: publisher connchanged;
    /* Pool configuration. */
private: connpoolconfig const config;
    /* Set when it's time to shut down.  The maintenance thread is
     * guranteed to terminate quickly once this is set. */
private: waitbox<void> shutdown;
    /* Beacon client used for slavename -> peername lookups. */
private: beaconclient *const bc;
    /* All of the connections and partial connections which currently
     * exist in this pool. */
private: list<pooledconnection *> connections;
    /* Connection maintenance thread. */
private: connpoolmaintenance *const maintain;

    /* Constructor for connpool. The beaconclient is used to perform
     * slavename -> peername lookups, when necessary.  We expose the
     * connpool status interface over @cs, if it is non-NULL. */
public:  connpool(beaconclient *,
                  const connpoolconfig & = connpoolconfig::dflt());

    /* Obtain a pooledconnection for a given remove slavename from the
     * pool.  The returned connection remains valid at least until you
     * call ->put() on it.  Note that this (a) runs quickly and (b)
     * never fails.  This is because it only starts the connection
     * state machine running in the background, rather than waiting
     * for it to complete.  That's appropriate because the connection
     * pool transparently handles disconnecting and reconnecting as
     * necessary to recover from remote machines crashing and
     * restarting, or even moving around, so errors reported at this
     * stage would be pretty meaningless anyway. */
public:  pooledconnection *connect(const slavename &);

    /* Status interface. */
public:  typedef connpoolstatus status_t;
public:  status_t status() const;

    /* Caller must ensure that all connections obtained from connect()
     * have already been put()ed. */
public:  ~connpool(); };

namespace tests {
void _connpool(); }

#endif /* !CONNPOOL_H__ */
