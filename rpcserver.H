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
template <typename> class rpcinterface;

template <typename cookie_t>
class rpcregistration {
    friend class rpcconnthread<cookie_t>;
    friend class rpcserver<cookie_t>;
private: mutex_t mux;
private: cond_t idle;
private: rpcserver<cookie_t> *owner;
private: list<rpcinterface<cookie_t> *> content;
private: unsigned outstanding;
private: ~rpcregistration() {}

    /* Interface to rpcserver */
private: rpcregistration(rpcserver<cookie_t> *_owner);
private: void start();
private: void finished();

private: rpcregistration() = delete;
private: rpcregistration(const rpcregistration &) = delete;
private: void operator=(const rpcregistration &) = delete;
    /* Prevent any further invocations of the registered interface or
       interfaces, Wait for any extant invocations to finish, and then
       release the rpcregistration structure. */
    /* Not a destructor because of non-trivial wait rules */
public:  void destroy();
};

template <typename cookie_t>
class rpcinterface {
    friend class rpcconnthread<cookie_t>;
    friend class rpcserver<cookie_t>;
private:   wireproto::msgtag tag;
private:   virtual maybe<error> message(const wireproto::rx_message &,
                                        cookie_t cookie,
                                        buffer &) = 0;
protected: rpcinterface(wireproto::msgtag _tag);
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
    private: void run();
    public: rootthread(rpcserver *_owner);
    };
private:   class unknowniface : public rpcinterface<cookie_t> {
    public:  unknowniface();
    private: maybe<error> message(const wireproto::rx_message &,
                                  cookie_t cookie,
                                  buffer &);
    };
private:   mutex_t mux;
private:   list<rpcregistration<cookie_t> *> registrations;
private:   waitbox<bool> shutdown;
private:   waitqueue<rpcconnthread<cookie_t> *> dying;
private:   listenfd sock;
private:   thread *roothandle;
private:   rootthread root;
private:   unknowniface unknowninterface;
private:   rpcregistration<cookie_t> *unknownregistration;
    /* I'd really like to make this private (we should only ever be
       destroyed by calling destroy()), but g++ won't let you create
       derived classes from anything with a private destructor. */
protected: virtual ~rpcserver();
private:   rpcserver(const rpcserver &) = delete;
private:   void operator=(const rpcserver &) = delete;
    
    /* Interface to rpcregistration */
    /* Remove a registration from the list, preventing any further
       invocations, without waiting for extant invocations to
       finish. */
private:   void deregister(rpcregistration<cookie_t> *);
    
    /* Construct an RPC server which does nothing at all. */
protected: rpcserver();
    
    /* Called by the rpcserver implementation whenever a new
       connection arrives.  If this returns an error then the
       connection will be dropped and endconn() called immediately.
       Called on the connection thread. */
private:   virtual orerror<cookie_t> startconn(rpcconn &conn) = 0;
    
    /* Called by the rpcserver implementation whenever a connection
       ends, once all method invocations on that connection have
       finished. */
private:   virtual void endconn(cookie_t) = 0;
    
    /* Callback from derived classes when a connection needs to be
     * torn down. */
public:    void killconn(rpcconn &conn);
    
    /* Register a new RPC interface with the server.  The interface
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
        friend class rpcserver<cookie_t>;
    private: multiregistration(const multiregistration &) = delete;
    private: void operator=(const multiregistration &) = delete;
    private: list<rpcinterface<cookie_t> *> content;
    public:  multiregistration() : content() {};
    public:  multiregistration &add(rpcinterface<cookie_t> &);
    public:  ~multiregistration();
};
public:    rpcregistration<cookie_t> *registeriface(const multiregistration &r);
    
    /* Start accepting new incoming operations, usually called after
       you've finished registering all of the needed interfaces.
       Abort if the interface is already running. */
protected: maybe<error> start(const peername &p, const fields::field &name);
    
    /* Get a peername on with which other hosts can connect to this
     * server */
public:    peername localname() const;
    
    /* Stop accepting new incoming operations and wait for any extant
       ones to finish, usually called just before we shut down.  No-op
       if we've already stopped. */
protected: void stop();
    
    /* Release the server, stopping it first if necessary.  All RPC
       interfaces must have been unregistered prior to calling
       this one. */
    /* Not a true destructor because it has non-trivial wait rules and
       a non-trivial precondition. */
protected: void destroy();
};

#endif /* !RPCSERVER_H__ */
