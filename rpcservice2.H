#ifndef RPCSERVICE2_H__
#define RPCSERVICE2_H__

#include <functional>

#include "beaconserver.H"
#include "listenfd.H"
#include "mutex.H"
#include "peername.H"
#include "proto2.H"

class clientio;
class deserialise1;
class fd_t;
namespace tests { template <typename, typename ...> class hookpoint; }
template <typename> class maybe;
template <typename> class nnp;
template <typename> class orerror;
class serialise1;
template <typename> class waitbox;

/* The double limit is necessary: maxoutstandingcalls prevents
 * overload if the client gives us a lot of small but expensive calls,
 * and txbufferlimit bounds the amount of memory used if the client
 * sends us a lot of cheap calls with large responses. */
class rpcservice2config {
    /* Beacon configuration */
public: beaconserverconfig beacon;
    /* Limit on the number of outstanding incompletecalls we can have
     * before we stop accepting more requests from the client. */
public: unsigned maxoutstandingcalls;
    /* Limit on the number of pending bytes in our TX buffer before we
     * stop accepting more requests from the client. */
public: unsigned txbufferlimit;
public: rpcservice2config(const beaconserverconfig &_beacon,
                          unsigned _maxoutstandingcalls,
                          unsigned _txbufferlimit);
    /* Pick some sensible defaults for all parameters. */
public: static rpcservice2config dflt(const clustername &,
                                      const slavename &); };

class rpcservice2 {
private:   class rootthread;
private:   class connworker;

    friend class rootthread;
    friend class connworker;

private:   const rpcservice2config config;
private:   rootthread *const root;

private:   static orerror<listenfd> open(const peername &pn);
private:   orerror<void> _initialise(clientio);

    /* Start listening on an address.  @t gives the type of service to
     * construct (and must be derived class of rpcservice2).  @pn
     * gives the interface to listen on and @config the service
     * configuration to use; other arguments are passed through
     * verbatim to @t's constructor.  rpcservice2 starts listening
     * before calling the derived class constructor, so other hosts
     * can connect as soon as the constructor is called.  The
     * constructor will not be called until we are certain that the
     * setup will succeed.  The derived class must be able to process
     * incoming calls as soon as its constructor returns. */
public:    template <typename t, typename ... args> static orerror<nnp<t> >
    listen(clientio,
           const rpcservice2config &config,
           const peername &pn,
           args &&...);
    /* Convenience wrapper for listen() which uses the default configuration. */
public:    template <typename t, typename ... args> static orerror<nnp<t> >
    listen(clientio,
           const clustername &cn,
           const slavename &sn,
           const peername &pn,
           args &&...);

    /* Hook for derived classes to do some initialisation of their
     * own.  This is called after we start listening on the socket (so
     * remote hosts can connect() without error) but before we accept
     * any calls and before listen() returns.  If it returns an error
     * then the service will be torn back down and the same error
     * returned from listen().  The default implementation does
     * nothing and returns Success. */
public:    virtual orerror<void> initialise(clientio);

    /* Cookie type: our constructor can only be called from listen(),
     * indirectly via the derived type constructor, and requires one
     * of these cookies, and the only way of getting such a cookie is
     * by calling listen(). */
protected: class constoken {
        friend class rpcservice2;
    private: constoken() = delete;
    private: constoken(const constoken &) = delete;
    private: listenfd fd;
    private: const peername &pn;
    private: const rpcservice2config &config;
    private: constoken(listenfd _fd,
                       const peername &_pn,
                       const rpcservice2config &_config)
        : fd(_fd),
          pn(_pn),
          config(_config) {} };

    /* Use listen() instead. */
protected: rpcservice2(const constoken &, const list<interfacetype> &);
protected: rpcservice2(const constoken &, interfacetype);

    /* Extract the port we're listening on.  We deliberately don't try
     * to provide the hostname part of the address, because we don't
     * know, at this level, what address a given remote client would
     * need to connect to in order to reach us (e.g. if there are
     * multiple NICs and you need to use a particular one). */
public:    peername::port port() const;

    /* Cookie type used to indicate that a particular callback will be
     * invoked on the connection thread, and hence potentially slow
     * down superficially unrelated RPCs if it takes too long. */
public:    class onconnectionthread {
        friend class rpcservice2;
    private: onconnectionthread(); };

    /* The actual call interface, used to implement the bulk of the
     * service-specific processing.  called will be called whenever we
     * receive a call from a remote peer, directly from the main
     * connection thread.  It receives a deserialiser set up to parse
     * the message received from the client (excluding the header);
     * note that this deserialiser will become invalid as soon as
     * called() returns, even if the incompletecall is still
     * outstanding, so called() must do all necessary parsing
     * synchronously.
     *
     * It also receives an incompletecall structure which it can use
     * to complete the call later on.  The call does not have to be
     * completed before called() returns (but note that leaving an
     * excessive number of calls outstanding will consume the
     * maxoutstandingcalls quota in the configuration).  The called()
     * method can either return Success, indicating that it has
     * accepted the call and will eventually complete or fail @ic, or
     * an error, indicating that the call is invalid, in which case
     * the rpcservice2 implementation will discard the call and call
     * ic->fail() with the error.  called() should only return Success
     * if it has parsed the *entire* client message; leaving stuff
     * behind is an error and will cause the client connection to
     * drop.
     *
     * called() receives a clientio token, so can wait if necessary,
     * but note, first, that it is called from the connection thread,
     * so will stop most other processing on this connection when it
     * does so, and, second, that the client might be disconnected
     * while it's waiting.  To avoid unnecessary long delays called()
     * should monitor ic->abandoned() while it's waiting and exit
     * early if appropriate. */
public:    class incompletecall;
private:   virtual orerror<void> called(
        clientio,
        onconnectionthread,
        deserialise1 &ds,
        interfacetype,
        nnp<incompletecall> ic) = 0;

public:    class incompletecall {
        friend rpcservice2;
    private: incompletecall() = delete;
    private: incompletecall(const incompletecall &) = delete;
    private: void operator=(const incompletecall &) = delete;
    private: incompletecall(connworker &_owner, proto::sequencenr _seqnr);

    private: connworker &owner;
    private: proto::sequencenr const seqnr;

        /* Receive a waitbox which will be set if the client has
         * abandoned the call (usually by disconnecting).  If this is
         * set then the client will never receive the results of the
         * call and so the service implementation should abort
         * quickly.  The service implementation must arrange that it
         * does not wait for a long time once the abandoned waitbox is
         * set. */
    public:  const waitbox<void> &abandoned() const;
        /* Mark a call as complete.  @doit will be called
         * (synchronously, before complete() returns) with a
         * serialiser set up to serialise directly into the connection
         * TX buffer.  Note that doit is called under the TX lock, so
         * will prevent any other calls completing while it runs.
         * complete() will release the incompletecall before it
         * returns; it must not be referenced again.  This function is
         * safe to call whether on or off the connection thread. */
    public:  void complete(
        const std::function<void (serialise1 &s, mutex_t::token txlock)> &doit);
        /* Mark a call as failed.  A reply is sent to the client
         * consisting of a simple error and the incompletecall
         * structure is released. */
    public:  void fail(error);
        /* Specialised versions of complete() and fail() which run
         * slightly faster than the generic versions but which can
         * only be safely called from the connection thread.  Apart
         * from that, and the addition of an onconnectionthread token
         * to the complete() @doit callback, these are semantically
         * identical to the generci versions above. */
    public:  void complete(
        const std::function<void (serialise1 &s,
                                  mutex_t::token txlock,
                                  onconnectionthread)> &doit,
        onconnectionthread);
    public:  void fail(error, onconnectionthread);
        /* Further variants of complete() for use when a call produces
         * no outputs other than a success/failure indication. */
    public:  void complete(orerror<void>);
    public:  void complete(orerror<void>, onconnectionthread);
        /* Use complete() or fail() instead. */
    private: ~incompletecall();};

    /* Destroy an RPC service, waiting for any extant calls to
     * called() to return and for any extant incomplete call
     * structures to be complete()d or fail()d.  Any outstanding
     * incompletecalls will have their abandoned() waitbox set. */
public:    void destroy(clientio);

    /* Hook for derived classes to do something when we're shutting
     * down.  This is called after the last call is complete and once
     * remote peers can no longer connect to the interface.  destroy()
     * will wait for it to complete before returning, and before
     * releasing the class's memory.  The default implementation does
     * nothing.  Note that this will never be called if initialise()
     * returns an error. */
private:   virtual void destroying(clientio);

    /* Logically private (only called from destroy()) but needs to be
     * protected to keep gcc happy. */
protected: virtual ~rpcservice2();

    /* Test interface. */
public:    static tests::hookpoint<void> clientdisconnected;
};


#endif /* !RPCSERVICE2_H__ */
