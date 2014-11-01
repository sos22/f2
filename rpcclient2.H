#ifndef RPCCLIENT2_H__
#define RPCCLIENT2_H__

#include "mutex.H"
#include "proto2.H"
#include "pubsub.H"

class deserialise1;
class error;
template <typename> class maybe;
template <typename> class nnp;
template <typename> class orerror;
class peername;
class serialise1;

class rpcclient2 {
public:  template <typename> class asynccall;
public:  class asyncconnect;
private: class _asynccall;
private: class workerthread;
public:  class onconnectionthread;

private: rpcclient2(const rpcclient2 &) = delete;
private: void operator=(const rpcclient2 &) = delete;

    /* The rpcclient2 structure is actually embedded in the
     * workerthread structure.  This returns the enclosing
     * structure. */
private: nnp<workerthread> worker() const;
    /* Add an asynccall to the pending receive list.  Returns
     * error::disconnected() if the connection has already shut
     * down. */
private: orerror<void> queuerx(nnp<_asynccall> call);
    /* Add something to the pending transmit queue. */
private: void queuetx(const std::function<void (serialise1 &,
                                                mutex_t::token)> &serialise,
                      proto::sequencenr seqnr);
    /* Publisher for asynccalls to notify when they get aborted. */
private: publisher &abortedpub();
    /* Bits which are common to every possible return type for
     * asynccalls. */
private: class _asynccall {
    public:    mutable mutex_t mux;
    public:    bool aborted;
    public:    bool _finished;
    public:    publisher _pub;
    public:    publisher &abortedpub;
    public:    proto::sequencenr seqnr;
    public:    _asynccall(publisher &_abortedpub);
    public:    virtual void complete(deserialise1 &, onconnectionthread) = 0;
    public:    virtual void fail(error, onconnectionthread) = 0;
    public:    class token {
            friend class _asynccall;
        private: token() {}; };
    public:    maybe<token> finished() const;
    public:    token wait(clientio) const;
    public:    virtual ~_asynccall(); };
    /* Use connect. */
private: rpcclient2();

    /* Asynchronous connect interface.  Start a connect sequence
     * aganist a given remote peer and immediately return a cookie
     * which can be used to receive the results of the call, or to
     * abort before the connect finishes. */
public:  class asyncconnect;
public:  static nnp<asyncconnect> connect(const peername &);
public:  class asyncconnect {
        friend class rpcclient2;
    private: asyncconnect(const asyncconnect &) = delete;
    private: void operator=(const asyncconnect &) = delete;
    private: nnp<workerthread> owner() const;
        /* Use connect() */
    private: asyncconnect();

        /* Check whether the connect has completed.  Returns either
         * Nothing, if it hasn't, or a token which can be used to
         * extract the results using pop() if it has. */
    public:  class token {
        friend class asyncconnect;
        private: token(); };
    public:  maybe<token> finished() const;

        /* Publisher which will be signalled when finished() goes from
         * Nothing to something. */
    public:  const publisher &pub() const;
        /* Extract the result of the connect (once finished() returns
         * non-Nothing).  Releases the asyncconnect structure; do not
         * attempt to access it again. */
    public:  orerror<nnp<rpcclient2> > pop(token);
        /* Abort an attempt to connect to a remote peer (or disconnect
         * if the connect has already completed).  Releases the
         * asyncconnect structure. */
    public:  void abort();
        /* Use pop() or abort() instead. */
    private: ~asyncconnect(); };

    /* Convenience wrapper around the async connect interface.  Start
     * a connect, wait for it to complete, and then return the
     * result. */
public:  static orerror<nnp<rpcclient2> > connect(
    clientio,
    const peername &,
    maybe<timestamp> deadline = Nothing);

    /* Tag type used to indicate that a call can hold up the
     * connection state machine if it blocks for too long. */
public:  class onconnectionthread {
        friend class rpcclient2;
    private: onconnectionthread(); };

    /* Asynchronous call interface.  Start a call against the remote
     * peer and return a cookie allowing you to extract the results
     * once it completes.
     *
     * @serialise is used to generate the message which is to be sent.
     * It is called under the TX lock with @s set up to serialise
     * directly into the TX buffer and with a message header already
     * in place.  ::call() will run the serialise routine
     * synchronously to completion before it returns
     *
     * @deserialise is used to parse the response once it's received.
     * @deserialise will always be called for any asyncall which is
     * not abort()ed, even those which fail.  @d is either an error,
     * if the call failed, or a deserialiser set up to parse the
     * response.  Note that deserialise methods are invoked on the
     * connection thread, and so may (but are not guaranteed to) stop
     * other activity on this connection while they're running.  The
     * return value of @deserialise will provide the return value of
     * the asynccall pop() method, when it's called.
     *
     * deserialise must always consume the entire message, and no
     * more.  Anything else is an error which will render the
     * connection unusable.
     *
     * The deserialise method also receives a reference to the
     * asynccall structure associated with the call.  This should be
     * treated with care: most asynccall methods, including abort()
     * and pop(), wait for the deserialise routine to finish, if it is
     * already running when they're called, and so calling one from
     * here will lead to an immediate deadlock.  @ac is only really
     * useful for passing off to another thread.
     */
public:  template <typename> class asynccall;
public:  template <typename t> nnp<asynccall<t> > call(
        const std::function<void (serialise1 &s, mutex_t::token txlock)> &
            serialise,
        const std::function<orerror<t> (asynccall<t> &ac,
                                        orerror<nnp<deserialise1> > d,
                                        onconnectionthread)> &deserialise);
public:  template <typename t> class asynccall final : private _asynccall {
        friend class rpcclient2;
    private: asynccall() = delete;
    private: asynccall(const asynccall &) = delete;
    private: maybe<orerror<t> > res;
    private: std::function<orerror<t> (class asynccall<t> &,
                                       orerror<nnp<deserialise1> >,
                                       onconnectionthread)> deserialise;

    private: asynccall(
        const std::function<orerror<t> (class asynccall<t> &,
                                        orerror<nnp<deserialise1> >,
                                        onconnectionthread)> &,
        publisher &_aborted);

    private: void complete(deserialise1 &, onconnectionthread oct);
    private: void fail(error, onconnectionthread oct);
        /* Check whether an asynchronous call has finished.  Returns
         * either Nothing, if it hasn't, or a token which can be used
         * to pop() the result if it has.  This will become something
         * shortly after the call's deserialise method returns. */
    public:  maybe<token> finished() const { return _asynccall::finished(); }
        /* Publisher which is signalled when finished() goes from
         * Nothing to something. */
    public:  const publisher &pub() const { return _pub; }
        /* Obtain the result of the call, once it has finished.  This
         * returns whatever deserialise() returned. */
    public:  orerror<t> pop(token);
        /* Attempt to abort a call.  If deserialise() has not yet been
         * called, this returns Nothing (and prevents deserialise()
         * from ever being called on this asynccall).  If
         * deserialise() has been called then this returns whatever
         * deserialise() did.  If deserialise() is still running then
         * this waits until deserialise() finishes and then returns
         * whatever it returned.  In any case, the asynccall structure
         * is released and may not be used again. */
    public:  maybe<orerror<t> > abort(clientio);
        /* Convenience wrapper around finished(), pub(), and pop():
         * wait for finished() to become non-Nothing and then pop()
         * and return the result of the call. */
    public:  orerror<t> pop(clientio io) { return pop(wait(io)); }
        /* Use pop() or abort() instead. */
    private: ~asynccall() {} };

    /* Convenience wrapper around the asynchronous call mechanism.
     * Start a call and then pop() the result.  Note that the
     * deserialise() calling convention is slightly different:
     * deserialise() is only invoked for successful calls, with errors
     * returned verbatim, and deserialise() does not receive an
     * asyncconnect structure. */
public:  template <typename t> orerror<t> call(
    clientio,
    const std::function<void (serialise1 &,
                              mutex_t::token txlock)> &serialise,
    const std::function<orerror<t> (deserialise1 &,
                                    onconnectionthread)> &deserialise,
    maybe<timestamp> deadline = Nothing);

    /* Destroy an RPC client connection.  No more calls may be
     * started; any outstanding calls will fail with
     * error::disconnected. */
public:  void destroy();

    /* Use destroy() instead. */
private: ~rpcclient2(); };

#endif /* !RPCCLIENT2_H__ */
