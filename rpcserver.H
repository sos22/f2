#ifndef RPCSERVER_H__
#define RPCSERVER_H__

#include "cond.H"
#include "list.H"
#include "listenfd.H"
#include "mutex.H"
#include "thread.H"
#include "waitbox.H"
#include "waitqueue.H"
#include "wireproto.H"

class buffer;
class error;
template <typename> class maybe;
class peername;
class rpcconn;

template <typename> class rpcconnthread;
template <typename> class rpcserver;
template <typename> class rpcservice;
template <typename> class rpcinterface;

template <typename cookie_t>
class rpcregistration {
    friend class rpcservice<cookie_t>;
    friend class rpcconnthread<cookie_t>;
private: mutex_t mux;
private: cond_t idle;
private: rpcservice<cookie_t> *owner;
private: list<rpcinterface<cookie_t> *> content;
private: unsigned outstanding;

private: rpcregistration() = delete;
private: rpcregistration(const rpcregistration &) = delete;
private: void operator=(const rpcregistration &) = delete;

private: rpcregistration(rpcservice<cookie_t> *_owner);
    /* Someone started using this interface.  Prevent it disappearing
     * until they're finished(). */
private: void start();
    /* Whoever called start() is now finished with the interface.
     * Wake up anyone waiting for the ref to drop away and potentially
     * tear the interface down. */
private: void finished();
    /* Prevent any further invocations of the registered interface or
       interfaces, Wait for any extant invocations to finish, and then
       release the rpcregistration structure. */
    /* Not a destructor because of non-trivial wait rules */
public:  void destroy();

private: ~rpcregistration(); };

template <typename cookie_t>
class rpcinterface {
    friend class rpcconnthread<cookie_t>;
    friend class rpcservice<cookie_t>;
private:   wireproto::msgtag tag;
protected: rpcinterface(wireproto::msgtag _tag);
private:   virtual maybe<error> message(const wireproto::rx_message &,
                                        cookie_t cookie,
                                        buffer &) = 0;
};

/* An RPC service represents a collection of RPC interfaces which are
   exported over the same socket. */
template <typename cookie_t>
class rpcservice {
    friend class rpcregistration<cookie_t>;
private:   class unknowniface : public rpcinterface<cookie_t> {
    public:  unknowniface();
    private: maybe<error> message(const wireproto::rx_message &,
                                  cookie_t cookie,
                                  buffer &);
    };
private:   mutex_t mux;
private:   list<rpcregistration<cookie_t> *> registrations;
private:   unknowniface unknowninterface;
private:   rpcregistration<cookie_t> *const unknownregistration;
private:   rpcservice(const rpcservice &) = delete;
private:   void operator=(const rpcservice &) = delete;
    
    /* Construct a new empty service.  Until some interfaces are
       registered, the service returns unrecognisedmessage for every
       request. */
public:    rpcservice();

    /* Register a new RPC interface with the service.  The interface
       can be invoked at any time after this is called until it us
       unregistered.  It is an error to register multiple interfaces
       with the same message tag.  Interfaces can be deregistered with
       the rpcregistration::deregister() method. */
public:    rpcregistration<cookie_t> *registeriface(rpcinterface<cookie_t> &ri);
    /* Register a group of RPC interfaces in one go.  Note that
       registration is not guaranteed to be atomic with respect to
       incoming calls i.e. if registering interfaces A and B at once
       there may be a window in which A is accessible and B not or
       vice versa.  Interfaces which are registered as a group must be
       deregistered as a grouop (although, as with registation, there
       is no guarantee that deregistration happens atomically). */
public:    class multiregistration {
        friend class rpcservice<cookie_t>;
    private: multiregistration(const multiregistration &) = delete;
    private: void operator=(const multiregistration &) = delete;
    private: list<rpcinterface<cookie_t> *> content;
    public:  multiregistration() : content() {};
    public:  multiregistration &add(rpcinterface<cookie_t> &);
    public:  ~multiregistration();
};
public:    rpcregistration<cookie_t> *registeriface(const multiregistration &r);

    /* Lookup the registered interface for a given tag.  Returns the
       registration and the interface (which might be the unknown
       interface for unrecognised tags).  The registration is returned
       with its reference count incremented; call reg->finish() once
       you're done with it. */
    struct lookupres {
        rpcregistration<cookie_t> *reg;
        rpcinterface<cookie_t> *iface; };
public:    lookupres lookup(wireproto::msgtag);

    /* Remove a registration from the list, preventing any further
       invocations, without waiting for extant invocations to finish.
       Should only be invoked by rpcregistration. */
private:   void deregister(rpcregistration<cookie_t> *);

    /* Destroy the service, unregistering any remaining interfaces.
     * Waits for any extant invocations on the service to finish. */
    /* Not a true destructor because it has non-trivial wait rules. */
public:    void destroy();

private:   ~rpcservice();
};

template <typename cookie_t> class rpcconnthread;

template <typename cookie_t>
class rpcserver {
    friend class rpcregistration<cookie_t>;
    friend class rpcconnthread<cookie_t>;

private:   class rootthread : public threadfn {
    public:  list<rpcconnthread<cookie_t> *> threads;
    public:  mutex_t threadsmux;
    private: rpcserver *owner;
    public:  rootthread(rpcserver *_owner);
    private: void run();
    };
public:    rpcservice<cookie_t> *service;
private:   mutex_t mux;
private:   waitbox<bool> shutdown;
private:   waitqueue<rpcconnthread<cookie_t> *> dying;
private:   listenfd sock;
private:   thread *roothandle;
private:   rootthread root;
private:   rpcserver(const rpcserver &) = delete;
private:   void operator=(const rpcserver &) = delete;
    
    /* Construct an RPC server which does nothing at all. */
protected: rpcserver();
    
    /* Start accepting new incoming operations, usually called after
       you've finished registering all of the needed interfaces.
       Abort if the interface is already running. */
protected: maybe<error> start(const peername &p, const fields::field &name);
    
    /* Called by the rpcserver implementation whenever a new
       connection arrives.  If this returns an error then the
       connection will be dropped and endconn() called immediately.
       Called on the connection thread. */
private:   virtual orerror<cookie_t> startconn(rpcconn &conn) = 0;
    
    /* Get a peername on with which other hosts can connect to this
     * server */
public:    peername localname() const;
    
    /* Callback from derived classes when a connection needs to be
     * torn down. */
public:    void killconn(rpcconn &conn);
    
    /* Called by the rpcserver implementation whenever a connection
       ends, once all method invocations on that connection have
       finished. */
private:   virtual void endconn(cookie_t) = 0;
    
    /* Stop accepting new incoming operations and wait for any extant
       ones to finish.  Leaves the server in an invalid state from
       which the only valid operation is the destructor.  No-op if
       we've already stopped. */
protected: void stop();

    /* Release a server, stopping it first if necessary. */
    /* Not a true destructor because it can wait for things. */
public:    void destroy();

    /* Ideally this would be private (destroy should be the only thing
       destructing these structures), but gcc doesn't allow that, for
       some reason. */
protected: virtual ~rpcserver();
};

#endif /* !RPCSERVER_H__ */
