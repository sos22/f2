#ifndef RPCSERVICE_H__
#define RPCSERVICE_H__

#include "thread.H"
#include "tuple.H"

class listenfd;
class peername;
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
    iter1(ctxt, 2, mand, value, unsigned, maxoutstanding)
class rpcserviceconfig {
    mktuplefields(rpcserviceconfig, public);
public: static const rpcserviceconfig dflt; };
mktupleext(rpcserviceconfig);

class rpcservicestatus {
};

class rpcservice {
private: class rootthread;
private: class worker;
    friend class rootthread;
    friend class worker;

    /* Intialised early in listen() and then never changed. */
private: rootthread *root;

private: rpcserviceconfig const config;

    /* Use listen() */
private: rpcservice();

    /* Start listening on a particular peername.  The service can
     * accept new client connections as soon as this returns.  @t must
     * be derived from rpcservice. */
public:  template <typename t, typename ... args>
    static orerror<t *> listen(
        const peername &,
        const rpcserviceconfig &config,
        args &&...);

    /* Thin wrapper around socket() and listen() */
private: static orerror<int> open(const peername &);

    /* rpcservice should only be constructed by listen(); this is
     * enforced by the constoken parameter. */
public:  class constoken {
        friend class rpcservice;
    private: const rpcserviceconfig &config;
        /* Use listen() */
    private: explicit constoken(const rpcserviceconfig &);
    private: constoken(const constoken &) = delete; };
protected: explicit rpcservice(const constoken &);

    /* Helper for the listen(). */
private:   void startrootthread(listenfd);

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
     * clientio token, and so should return completely.  There is no
     * requirement that the response be completed quickly, though. */
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
public:  class response {
        friend class worker;
        /* The response we're busy building. */
    private: wireproto::resp_message inner;
        /* The worker which will eventually transmit our results, or
         * NULL if it's been shut down.  Protected by the global
         * attachment lock. */
    private: worker *owner;
        /* Only allocated by the rpcserver implementation */
    private: response(const wireproto::rx_message &rxm,
                      worker *owner);
        /* Add a new parameter to the response which will eventually
         * be sent. */
    public:  template <typename t> response &addparam(
        wireproto::parameter<t>,
        const t &);
        /* All necessary parameters have now been added to this
         * response.  Queue it for transmission.  The caller must not
         * touch the response again after this has been called. */
    public:  void complete();
        /* Mark a call as failed.  All parameters are removed and the
         * remote caller receives an error indication from their call.
         * The local caller must not touch the response structure
         * after this has been called. */
    public:  void fail(error);
        /* Use complete() or fail() */
    private: ~response(); };
private: virtual void call(
    const wireproto::rx_message &req,
    response *resp) = 0;

    /* Status interface */
public:  typedef rpcservicestatus status_t;
public:  status_t status() const;

    /* Destroy an RPC service.  This will wait for any extant
     * invocations of the call() method to complete before running the
     * destructor. */
    /* Note that it waits for call()s to finish without a clientio
     * token.  That's okay because the call() method also lacks a
     * token. */
public:  void destroy();

    /* Use finishedpop() or destroy() instead. */
private: virtual ~rpcservice(); };

#endif /* !RPCSERVICE_H__ */
