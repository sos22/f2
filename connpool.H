/* Variant of rpcclient2 which hooks into the beacon, so you can do
 * calls directly against slavenames rather than needing to do the
 * peername lookup, and which tries to handle errors which are
 * susceptible to retransmission.  Conn pool users must specify a
 * timeout on every call; subject to that limit, the conn pool itself
 * is responsible for deciding an appropriate retransmit/reconnect
 * schedule.  The conn pool will also handle multiplexing multiple
 * users over a single connection, when appropriate; protocols should
 * be designed so that that is always safe. */
#ifndef CONNPOOL_H__
#define CONNPOOL_H__

#include "beaconclient.H"
#include "clientio.H"
#include "interfacetype.H"
#include "nnp.H"
#include "orerror.H"
#include "probability.H"
#include "timedelta.H"
#include "timestamp.H"

class publisher;
class slavename;

class connpool {
public:  class asynccall;
public:  class connlock;
public:  template <typename> class asynccallT;
public:  typedef void serialise(serialise1 &, connlock);
public:  typedef orerror<void> deserialise(
        orerror<nnp<deserialise1> >, connlock);

private: connpool(const connpool &) = delete;
private: connpool() {};
private: void operator=(const connpool &) = delete;

private: class impl;
private: impl &implementation();
private: const impl &implementation() const;

public:  class config {
        /* Beacon configuration. */
    public: beaconclientconfig beacon;
        /* Probability of forcing the initial send of a message to be
         * from the connection thread (i.e. the same context as
         * retransmits) even when it would normally be processed
         * directly at the call site.  That's always slower, but is
         * useful for exercising some corner cases of the API. */
    public: probability forceretry;
        /* How long should idle connections hang around before we
         * conclude they're no longer needed and garbage collect
         * them? */
    public: timedelta idletimeout;
        /* Timeout for connect() syscalls.  We'll continue retrying
         * the connect after hitting this, so it can be a bit shorter
         * than a normal connect timeout. */
    public: timedelta connecttimeout;
        /* Timeout for proto::hello requests.  Again, we'll retry if
         * this gets hit, so it doesn't need to be huge. */
    public: timedelta hellotimeout;
        /* How long to wait between connects to the same peername when
         * we hit an error other than a timeout? */
    public: timedelta debounceconnect;
    public: config(const beaconclientconfig &,
                   probability = probability::never);
    private: config(const beaconclientconfig &,
                    probability,
                    timedelta,
                    timedelta,
                    timedelta,
                    timedelta);
    public: static orerror<config> mk(
        const beaconclientconfig &,
        probability forceretry = probability::never,
        timedelta idletimeout = timedelta::seconds(60),
        timedelta connecttimeout = timedelta::seconds(10),
        timedelta hellotimeout = timedelta::seconds(10),
        timedelta debounceconnect = timedelta::seconds(1));
    public: static config dflt(const clustername &); };

    /* Tag type indicating that a particular method or callback is
     * invoked while holding the connection lock.  Holding the lock
     * means that no other operations can proceed on the connection at
     * the same time. */
public:  class connlock {
        friend connpool;
    private: connlock() {}
    };

    /* Deserialise method suitable for use with RPCs which return
     * nothing except for the mandatory success/failure indication. */
public:  static deserialise voidcall;

    /* Basic call interface.  Attempt to connect to a given remote
     * host and perform an RPC.
     *
     * This will automatically retry calls which encounter soft errors
     * (possibly calling serialise() multiple times as it does so) up
     * to the timeout, but will not retry hard errors.  Soft errors
     * include things like the beacon not being able to find the host
     * or the connect() syscall failing; basically, anything which
     * might have been caused by the remote machine crashing and
     * rebooting at the wrong time.
     *
     * Hard errors include things like the remote machine not
     * understanding the desired protocol or the remote service itself
     * returning an error.  Hard errors call every outstanding call
     * against a particular peer to fail.  Subsequent calls against
     * the same host will restart the connection state machine.  If
     * the conn pool cannot tell whether a particular error is hard or
     * soft it will generally err on the side of declaring it to be
     * soft and rely on timeouts to recover if it turns out to be
     * wrong.
     *
     * The serialise method will be called once for every time we have
     * to transmit the message (but should usually do the same thing
     * every time).  Note that this may be (in fact, usually will be)
     * after call() itself returns, so the serialise function should
     * not have references to e.g. the caller's local variables.  Note
     * also that serialise might *never* be called if we don't manage
     * to connect to the other peer before hitting the timeout.
     *
     * The deserialise method, on the other hand, is always invoked
     * precisely once for every call; either when we succeed or when
     * the call fails in a way which prevents retransmission
     * (including aborts, timeouts, and conn pool shutdown).  The
     * deserialise method will never be invoked until the final
     * serialise has finished and the asynccall finished() method will
     * not return success until after the deserialise has finished.
     * Note that the result of pop() is always the result of
     * deserialise so, for instance, a timed out call can return
     * Success if deserialise discards the error it is passed in. */
public:  nnp<asynccall> call(const slavename &sn,
                             interfacetype type,
                             timestamp deadline,
                             const std::function<serialise> &s,
                             const std::function<deserialise> &ds = voidcall);

    /* Cookie type representing an outstanding call.  Allocated by the
     * call() method. */
public:  class asynccall final {
        friend connpool;
        /* This should really be private, but I can't see how to do
         * the necessary friend declarations. */
    public:  class impl;
    private: asynccall(const connpool &) = delete;
    private: asynccall() {}
    private: void operator=(const asynccall &) = delete;
    private: impl &implementation();
    private: const impl &implementation() const;
        /* Proof of work type used to prove to pop() that the call has
         * already finished, miplying that it will return immediately
         * and does not need a clientio token.  Returned by
         * finished(). */
    public:  class token {
            friend class asynccall;
        private: token(); };
        /* Check whether the call has finished (which includes
         * success, hard failure, and timeout).  Returns a token type
         * which can be used as a proof-of-work to make pop()
         * non-blocking.  This will not return non-Nothing until the
         * deserialise method has run to completion. */
    public:  maybe<token> finished() const;
        /* Publisher which is notified at least when finished() becomes
         * non-Nothing. */
    public:  const publisher &pub() const;
        /* Convert a token into the result of the call, destroying the
         * asyncall structure in the process. */
    public:  orerror<void> pop(token);
        /* Abort a call, which may or may not be complete.  The
         * deserialise method for the call will be invoked with
         * error::aborted, if it has not already run.  This will not
         * return until deserialise has, and will return the result of
         * deserialise().  The asynccall structure is released. */
    public:  orerror<void> abort();
        /* Blocking variant of finished(): wait until the call's finished,
         * then return a token. */
    public:  token finished(clientio) const;
        /* Convenience wrapper: wait for finished() to become
         * non-Nothing and then pop() and return the result of the
         * call. */
    public:  orerror<void> pop(clientio io) { return pop(finished(io)); }
        /* Use abort() or pop() */
    private: ~asynccall(); };

    /* Convenience wrapper around the async call interface: start a
     * call, wait for it to complete, and then return the result. */
public:  orerror<void> call(clientio io,
                            const slavename &sn,
                            interfacetype type,
                            timestamp deadline,
                            const std::function<serialise> &s,
                            const std::function<deserialise> &ds = voidcall) {
        return call(sn, type, deadline, s, ds)->pop(io); }

    /* More convenient variant of the call interface for use when the
     * deserialise method needs to return a value.  Most of the
     * details of these are in connpool.tmpl. */
public:  template <typename> class asynccallT;
public:  template <typename t> nnp<asynccallT<t> > call(
    const slavename &sn,
    interfacetype type,
    timestamp deadline,
    const std::function<serialise> &s,
    const std::function<orerror<t> (orerror<nnp<deserialise1> >,
                                    connlock)> &ds);
public:  template <typename t> orerror<t> call(
    clientio io,
    const slavename &sn,
    interfacetype type,
    timestamp deadline,
    const std::function<serialise> &s,
    const std::function<orerror<t> (orerror<nnp<deserialise1> >,
                                    connlock)> &ds);

    /* Construct a new connpool. */
public:  static orerror<nnp<connpool> > build(const config &);
public:  static orerror<nnp<connpool> > build(const clustername &cn);

    /* Tear down a connpool.  Any outstanding calls will fail with
     * error::disconnected.  Note that this is quick (it has no
     * clientio token) and that it is safe even when there are still
     * outstanding calls.  Note also that outstanding calls fail but
     * are not abort()ed i.e. someone still needs to call pop() or
     * abort() on them.  destroy() will not return until the last call
     * to deserialise() has finished, but may return before the last
     * asynccall structure is released. */
public:  void destroy();

    /* Use destroy(). */
private: ~connpool();
};

namespace tests { void _connpool(); }

#endif /* !CONNPOOL_H__ */
