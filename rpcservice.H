#ifndef RPCSERVICE_H__
#define RPCSERVICE_H__

#include "listenfd.H"
#include "peername.H"
#include "thread.H"
#include "tuple.H"
#include "walltime.H"

class publisher;
class socket_t;
class subscriber;

#define _rpcserviceconfig(ctxt, iter0, iter1)                           \
    /* How many bytes can we have queued up to send on a connection at */ \
    /* one time?  This is in addition to the maxoutstanding count. */   \
    iter0(ctxt, 1, mand, value, unsigned, maxoutgoingbytes)             \
    /* Maximum number of outstanding calls per connection.  Once this*/ \
    /* limit is reached we stop polling for any more messages on the */ \
    /* connection. */                                                   \
    iter0(ctxt, 2, mand, value, unsigned, maxoutstanding)               \
    /* Socket receive buffer, in bytes.  Only really need to change */  \
    /* this in test cases. */                                           \
    iter0(ctxt, 3, opt, value, unsigned, socketrcvsize)                 \
    /* Socket send buffer, in bytes.  Only really need to change this */ \
    /* in test cases. */                                                \
    iter1(ctxt, 4, opt, value, unsigned, socketsendsize)
class rpcserviceconfig {
    mktuplefields(rpcserviceconfig, public);
public: static rpcserviceconfig dflt(); };
mktupleext(rpcserviceconfig);

#define _rpcserviceconnstatus(ctxt, iter0, iter1)                       \
    /* Who is connected to us?  Nothing if we're in the middle of */    \
    /* shutting down. */                                                \
    iter0(ctxt, 1, opt, ref, ::peername, remote)                        \
    /* When were they last active? */                                   \
    iter0(ctxt, 2, opt, ref, ::walltime, lastactive)                    \
    /* How many calls have they made, ever? */                          \
    iter0(ctxt, 3, mand, value, unsigned, callsever)                    \
    /* What state is the file descriptor in? */                         \
    iter0(ctxt, 4, opt, ref, ::fd_tstatus, fd)                          \
    /* Number of outstanding posted calls. */                           \
    iter0(ctxt, 5, mand, value, unsigned, currentcalls)                 \
    /* Bytes in the TX buffer */                                        \
    iter1(ctxt, 6, mand, value, size_t, txbytes)
class rpcserviceconnstatus {
    mktuplefields(rpcserviceconnstatus, public); };
mktupleext(rpcserviceconnstatus);

#define _rpcservicestatus(ctxt, iter0, iter1)                           \
    /* Current connections */                                           \
    iter0(ctxt, 1, mand, ref, list< ::rpcserviceconnstatus>, connections) \
    /* State of the listening socket */                                 \
    iter0(ctxt, 2, mand, ref, ::listenfdstatus, listenstatus)           \
    /* Where are we listening? */                                       \
    iter0(ctxt, 3, mand, ref, ::peername, listenon)                     \
    /* How many connections have we accepted, ever? */                  \
    iter0(ctxt, 4, mand, value, unsigned, connectionsever)              \
    /* How many connections have died with errors, ever? */             \
    iter1(ctxt, 5, mand, value, unsigned, errorsever)
class rpcservicestatus {
    mktuplefields(rpcservicestatus, public); };
mktupleext(rpcservicestatus);

class rpcservice {
private: class rootthread;
private: class worker;
    friend class rootthread;
    friend class worker;

    /* Intialised early in listen() and then never changed. */
private:   rootthread *root;

private:   rpcserviceconfig const config;

    /* Use listen() */
private:   rpcservice();

    /* Start listening on a particular peername.  The service can
     * accept new client connections as soon as this returns.  @t must
     * be derived from rpcservice. */
public:    template <typename t, typename ... args>
    static orerror<t *> listen(
        clientio,
        const rpcserviceconfig &config,
        const peername &,
        args &&...);
public:    template <typename t, typename ... args>
    static orerror<t *> listen(
        clientio,
        const peername &,
        args &&...);

    /* Thin wrapper around socket() and listen() */
private:   static orerror<listenfd> open(const peername &);

    /* rpcservice should only be constructed by listen(); this is
     * enforced by the constoken parameter. */
public:    class constoken {
        friend class rpcservice;
    private: const rpcserviceconfig &config;
        /* Use listen() */
    private: explicit constoken(const rpcserviceconfig &);
    private: constoken(const constoken &) = delete; };
protected: explicit rpcservice(const constoken &);

    /* Helper for listen(). */
private:   orerror<void> _initialise(clientio, listenfd);

    /* Initialisation callback.  This is called after we start
     * listening but before call() is invoked for the first time.  If
     * it returns an error then the service is torn back down again
     * (without calling destroying()) and listen() returns an error.
     * Otherwise, the server starts properly and will begin processing
     * remote calls.  The default implementation does nothing. */
private:  virtual orerror<void> initialise(clientio);

    /* Find the peername on which we're listening.  Note that the host
     * part of this is likely to be localhost; it's mostly useful for
     * the port number. */
    /* XXX seems error-prone. */
public:    peername localname() const;

    /* Hook so that services can specify how to respond to particular
     * messages.  This will be called for every message received on
     * the service (except PING and HELLO, which are handled
     * specially).  It is passed the received request and a response
     * structure which it must fill out to specify the kind of
     * response to generate.  The call() implementation must guarantee
     * that either fail() or complete() will be called on the response
     * structure, but there is no requirement that it do so before
     * returning, allowing asynchronous implementations of RPC
     * methods.  Note that the call() method itself does not receive a
     * clientio token, and so should return completely; failing to do
     * so can cause delays on related calls, with a high risk of
     * deadlock. */
    /* Note that we bound the number of calls which can be outstanding
     * for each connection.  Once that limit is reached, no further
     * requests will be read from the underlying socket, and so the
     * remote peer will not be able to start any more calls.  This is
     * necessary to ensure backpressure from slow services to fast
     * clients, making the whole system much more stable (in the
     * control theory sense).  The limit is specified in the service
     * config structure. */
    /* Note that services do not receive any indication when
     * connections are established and destroyed, or which connection
     * a particular request was sent over.  That is deliberate: the
     * underlying TCP connection can be destroyed and rebuilt at any
     * time as the beacon client and connection manager do their
     * thing; any time that the service behahviour depends on the TCP
     * socket used is a bug. */
    /* The rpcservice implementation will block destroy() until all
     * invocations of call() have finished and all responses have
     * either complete()d or fail()ed.  It does not automatically
     * perform any other synchronisation with asynchronous work
     * performed by derived classes i.e. once you call ->complete() on
     * the response the service's own destructor can be run.  See
     * rpcservice->destroying() if that's a problem for you. */
public:    class response {
        friend class worker;
        /* The response we're busy building. */
    private: wireproto::resp_message inner;
        /* The worker which will eventually transmit our results.
         * NULL once initialised. */
    private: worker *const owner;
        /* Only allocated by the rpcserver implementation */
    private: response(const wireproto::rx_message &rxm,
                      worker *owner);
        /* Add a new parameter to the response which will eventually
         * be sent. */
    public:  template <typename t> response &addparam(
        wireproto::parameter<t>,
        const t &);
        /* Extract a shutdown waitbox for the connection.  This will
         * be set if the worker cannot, for whatever reason, send the
         * response back to the client.  Derived classes should
         * arrange to call either complete() or fail() quickly once
         * this is set.  Once this is set complete() and fail() are
         * equivalent; the client won't receive the message,
         * anyway. */
    public:  const waitbox<void> &abandoned() const;
        /* All necessary parameters have now been added to this
         * response.  Queue it for transmission.  The caller must not
         * touch the response again after this has been called. */
    public:  void complete();
        /* Mark a call as failed.  All parameters are removed and the
         * remote caller receives an error indication from their call.
         * The local caller must not touch the response structure
         * after this has been called. */
    public:  void fail(error);
        /* Wrapper around complete() and fail(): call the former if
         * passed Success and the latter otherwise. */
    public:  void complete(orerror<void>);
        /* Use complete() or fail() */
    private: ~response();

        /* Testing interface: extract the socket which this request
         * came in on. */
    public:  socket_t __connection__() const; };
private:   virtual void call(
    const wireproto::rx_message &req,
    response *resp) = 0;

    /* Status interface */
public:    typedef rpcservicestatus status_t;
public:    status_t status() const;

    /* Destroy an RPC service.  This will wait for any extant
     * invocations of the call() method to return and for any
     * outstanding responses to be completed before invoking the
     * destructor. */
public:    void destroy(clientio);

    /* Optional callback to derived callback to perform final
     * destruction synchronisation.  This is called after the final
     * call to call() is complete and the final response has been
     * complete()d or fail()ed but before any destructor runs.
     * Derived classes can use it to perform any additional
     * synchronisation needed by any asynchronous work they start.
     * The default implementation does nothing. */
private:   virtual void destroying(clientio);

    /* Use finishedpop() or destroy() instead. */
protected: virtual ~rpcservice(); };

#endif /* !RPCSERVICE_H__ */
