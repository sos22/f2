/* Variant of rpcclient2 which hooks into the beacon, so you can do
 * calls directly against agentnames rather than needing to do the
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
#include "test.H"
#include "timedelta.H"
#include "timestamp.H"

class publisher;
class agentname;

class connpool {
public:  class connlock;
public:  template <typename> class asynccallT;
public:  typedef asynccallT<void> asynccall;
public:  template <typename> class deserialiserT;
public:  typedef deserialiserT<void> deserialiser;
public:  typedef void serialiser(serialise1 &, connlock);
    /* deserialise methods are called once we get a response from the
     * other side (or when we give up waiting).  They receive a
     * reference to the call structure.  Be careful with that: the
     * call isn't considered finished until deserialise returns, so
     * finished() will always return Nothing from a deserialise
     * methods and any of the asynccall wait methods (those with a
     * clientio token) will deadlock.  Basically, the only thing you
     * can do with the call reference is pass it off to the next stage
     * in your pipeline. */
public:  template <typename t> class deserialiserT {
        /* There are a whole bunch of different types of deserialiser.
         * This class handles converting between them. */
    public:  std::function<orerror<t> (
            asynccallT<t> &,
            orerror<nnp<deserialise1> >,
            connlock)> inner;
    public:  deserialiserT(const decltype(inner) &_inner) : inner(_inner) {}
    public:  deserialiserT(
        const std::function<
            orerror<t> (orerror<nnp<deserialise1> > e, connlock) > &f)
        : inner([f] (asynccallT<t> &,
                     orerror<nnp<deserialise1> > ds,
                     connlock cl) -> orerror<t> {
                    return f(ds, cl); }) {}
    public:  deserialiserT(
        const std::function<orerror<t> (deserialise1 &, connlock)> &f)
        : inner([f] (asynccallT<t> &,
                     orerror<nnp<deserialise1> > ds,
                     connlock cl) -> orerror<t> {
                    if (ds.isfailure()) return ds.failure();
                    else return f(ds.success(), cl); }) {}

    public:  deserialiserT()
        : inner([] (asynccallT<t> &,
                    orerror<nnp<deserialise1> > ds,
                    connlock) -> orerror<t> {
                    if (ds.isfailure()) return ds.failure();
                    else return Success; }) {}
    public:  orerror<t> operator()(asynccallT<t> &ac,
                                   orerror<nnp<deserialise1> > ds,
                                   connlock cl) const {
        return inner(ac, ds, cl); } };

private: connpool(const connpool &) = delete;
private: connpool() {};
private: void operator=(const connpool &) = delete;

private: class impl;
private: impl &implementation();
private: const impl &implementation() const;

public:  class config {
        /* Beacon configuration. */
    public: beaconclientconfig beacon;
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
    public: explicit config(const beaconclientconfig &);
    private: config(const beaconclientconfig &,
                    timedelta,
                    timedelta,
                    timedelta,
                    timedelta);
    public: static orerror<config> mk(
        const beaconclientconfig &,
        timedelta idletimeout = timedelta::seconds(60),
        timedelta connecttimeout = timedelta::seconds(10),
        timedelta hellotimeout = timedelta::seconds(10),
        timedelta debounceconnect = timedelta::seconds(1));
    public: static config dflt(const clustername &);
    public: bool operator==(const config &) const; };

    /* Tag type indicating that a particular method or callback is
     * invoked while holding the connection lock.  Holding the lock
     * means that no other operations can proceed on the connection at
     * the same time. */
public:  class connlock {
        friend connpool;
    private: connlock() {} };

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
     * returning an unrecoverable error.  Hard errors cause every
     * outstanding call against a particular peer to fail.  Subsequent
     * calls against the same host will restart the connection state
     * machine.  If the conn pool cannot tell whether a particular
     * error is hard or soft it will generally err on the side of
     * declaring it to be soft and rely on timeouts to recover if it
     * turns out to be wrong.
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
     * Success if deserialise discards the error it is passed in.
     *
     * The (intended) common case is for every call to have a
     * deadline, and the conn pool will automatically retry until that
     * deadline is reached.  The deadline can also be specified as
     * Nothing to cause the conn pool to retry forever.  That's only
     * really sensible if some higher level thing will perform its own
     * timeout processing and abort() any call which runs for too
     * long; otherwise, a crashed remote peer will cause the local
     * machine to hang forever as well, which is almost certainly not
     * what you want.
     */
public:  template <typename t> nnp<asynccallT<t> > _call(
    const agentname &sn,
    interfacetype type,
    maybe<timestamp> deadline,
    const std::function<serialiser> &s,
    const deserialiserT<t> &ds);
    /* Wrapper to help type inference along a little bit. */
public:  template <typename t, typename u> nnp<asynccallT<t> > call(
    const agentname &sn,
    interfacetype type,
    maybe<timestamp> deadline,
    const std::function<serialiser> &s,
    const u &ds);
public:  nnp<asynccallT<void> > call(
    const agentname &sn,
    interfacetype type,
    maybe<timestamp> deadline,
    const std::function<serialiser> &s) {
    return call<void>(sn, type, deadline, s, deserialiserT<void>()); }

    /* Helper for calling call() and then immediately waiting for the
     * result. */
public:  template <typename t> orerror<t> _call(
    clientio io,
    const agentname &sn,
    interfacetype type,
    maybe<timestamp> deadline,
    const std::function<serialiser> &s,
    const deserialiserT<t> &ds);
public:  template <typename t, typename u> orerror<t> call(
    clientio io,
    const agentname &sn,
    interfacetype type,
    maybe<timestamp> deadline,
    const std::function<serialiser> &s,
    const u &ds);
public:  orerror<void> call(
    clientio io,
    const agentname &sn,
    interfacetype type,
    maybe<timestamp> deadline,
    const std::function<serialiser> &s) {
    return _call<void>(io, sn, type, deadline, s, deserialiserT<void>()); }

    /* Construct a new connpool. */
public:  static orerror<nnp<connpool> > build(const config &);
public:  static orerror<nnp<connpool> > build(const clustername &cn);

    /* Get the conn pool's beacon client.  The beacon client remains
     * valid for as long as the conn pool is alive. */
public:  const ::beaconclient &beaconclient() const;

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

public:  static tests::hookpoint<void> reapedconnthread;
};

template <> nnp<connpool::asynccallT<void> > connpool::_call(
    const agentname &sn,
    interfacetype type,
    maybe<timestamp> deadline,
    const std::function<serialiser> &s,
    const deserialiserT<void> &ds);

/* Cookie type representing an outstanding call.  Allocated by the
 * call() method. */
template <> class
connpool::asynccallT<void> final {
    friend connpool;
    /* This should really be private, but I can't see how to do
     * the necessary friend declarations. */
public:  class impl;
private: asynccallT(const connpool &) = delete;
private: asynccallT() {}
private: void operator=(const asynccall &) = delete;
private: impl &implementation();
private: const impl &implementation() const;
    /* Proof of work type used to prove to pop() that the call has
     * already finished, miplying that it will return immediately and
     * does not need a clientio token.  Returned by finished(). */
public:  class token {
        friend class asynccallT;
    private: token(); };
    /* Check whether the call has finished (which includes success,
     * hard failure, and timeout).  Returns a token type which can be
     * used as a proof-of-work to make pop() non-blocking.  This will
     * not return non-Nothing until the deserialise method has run to
     * completion. */
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
    /* Convenience wrapper: wait for finished() to become non-Nothing
     * and then pop() and return the result of the call. */
public:  orerror<void> pop(clientio io) { return pop(finished(io)); }
    /* Use abort() or pop() */
private: ~asynccallT(); };

template <typename t> class connpool::asynccallT final {
    friend class connpool;
private: asynccall *inner;
private: maybe<orerror<t> > res;
private: asynccallT();
public:  class token {
        friend class asynccallT<t>;
    private: asynccall::token inner;
    private: explicit token(asynccall::token _inner) : inner(_inner) {}; };
public:  maybe<token> finished() const;
public:  orerror<t> pop(token tok);
public:  const publisher &pub() const;
public:  orerror<t> abort();
public:  token finished(clientio io) const;
public:  orerror<t> pop(clientio io);
private: ~asynccallT() { } };

template <typename t, typename u> nnp<connpool::asynccallT<t> >
connpool::call(
    const agentname &sn,
    interfacetype type,
    maybe<timestamp> deadline,
    const std::function<serialiser> &s,
    const u &ds) {
    return _call(sn, type, deadline, s, deserialiserT<t>(ds)); }

#endif /* !CONNPOOL_H__ */
