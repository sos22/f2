#ifndef RPCSERVER_H__
#define RPCSERVER_H__

#include "cond.H"
#include "list.H"
#include "listenfd.H"
#include "mutex.H"
#include "thread.H"
#include "wireproto.H"

class buffer;
class error;
template <typename> class maybe;
class peername;
template <typename> class waitbox;
template <typename> class waitqueue;

class rpcserver;
class rpcinterface;

class rpcregistration {
    friend class rpcserver;
private: mutex_t mux;
private: cond_t idle;
private: rpcserver *owner;
private: list<rpcinterface *> content;
private: unsigned outstanding;
private: ~rpcregistration() {}

    /* Interface to rpcserver */
private: rpcregistration(rpcserver *_owner);
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

class rpcinterface {
    friend class rpcserver;
private:   wireproto::msgtag tag;
private:   virtual maybe<error> message(const wireproto::rx_message &,
                                        buffer &) = 0;
protected: rpcinterface(wireproto::msgtag _tag);
};

class rpcserver {
    friend class rpcregistration;
    class clientthread;

private:   class rootthread : public threadfn {
    private: rpcserver *owner;
    private: void run();
    public: rootthread(rpcserver *_owner);
    };
private:   class unknowniface : public rpcinterface {
    public:  unknowniface();
    private: maybe<error> message(const wireproto::rx_message &,
                                  buffer &);
    };
private:   mutex_t mux;
private:   list<rpcregistration *> registrations;
private:   waitbox<bool> *shutdown;
private:   waitqueue<clientthread *> *dying;
private:   listenfd sock;
private:   thread *roothandle;
private:   rootthread root;
private:   unknowniface unknowninterface;
private:   rpcregistration *unknownregistration;
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
private:   void deregister(rpcregistration *);

    /* Construct an RPC server which does nothing at all. */
protected: rpcserver();
    /* Register a new RPC interface with the server.  The interface
       can be invoked at any time after this is called until it us
       unregistered.  It is an error to register multiple interfaces
       with the same message tag.  Interfaces can be deregistered with
       the rpcregistration::deregister() method. */
public:  rpcregistration *registeriface(rpcinterface &ri);
    /* Register a group of RPC interfaces in one go.  Note that
       registration is not guaranteed to be atomic with respect to
       incoming calls i.e. if registering interfaces A and B at once
       there may be a window in which A is accessible and B not or
       vice versa.  Interfaces which are registered as a group must be
       deregistered as a grouop (although, as with registation, there
       is no guarantee that deregistration happens atomically). */
public:  class multiregistration {
        friend class rpcserver;
    private: multiregistration(const multiregistration &) = delete;
    private: void operator=(const multiregistration &) = delete;
    private: list<rpcinterface *> content;
    public:  multiregistration() : content() {};
    public:  multiregistration &add(rpcinterface &);
    public:  ~multiregistration();
};
public:  rpcregistration *registeriface(const multiregistration &r);
    /* Start accepting new incoming operations, usually called after
       you've finished registering all of the needed interfaces.
       Abort if the interface is already running. */
protected: maybe<error> start(const peername &p);
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
