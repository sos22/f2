#ifndef RPCSERVER_H__
#define RPCSERVER_H__

#include "rpcconnthread.H"
#include "listenfd.H"
#include "thread.H"
#include "waitbox.H"

template <typename> class list;
template <typename> class orerror;
class peername;
class rpcconn;

template <typename> class rpcservice;
template <typename> class rpcregistration;

template <typename cookie_t>
class rpcserver {
    friend class rpcregistration<cookie_t>;
    friend class rpcconnthread<cookie_t>;

private:   class rootthread : public threadfn {
    private: class startconn : public rpcconnthread<cookie_t>::startconn {
        private: rootthread &owner;
        public:  startconn(rootthread &_owner);
        public:  orerror<cookie_t> operator()(clientio, rpcconn &conn);
        };
    private: class endconn : public rpcconnthread<cookie_t>::endconn {
        private: rootthread &owner;
        public:  endconn(rootthread &_owner);
        public:  void operator()(clientio, cookie_t);
        };
    public:  startconn startconn_;
    public:  endconn endconn_;
    public:  list<rpcconnthread<cookie_t> *> threads;
    public:  mutex_t threadsmux;
    private: rpcserver *owner;
    public:  subscriber sub;
    public:  rootthread(rpcserver *_owner);
    private: void run(clientio);
    };
public:    rpcservice<cookie_t> *service;
private:   mutex_t mux;
private:   waitbox<bool> shutdown;
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
    
    /* Register a connection thread with the root thread. */
private: void _startconn(rpcconnthread<cookie_t> *);
    
    /* Called by the rpcserver implementation whenever a new
       connection arrives.  If this returns an error then the
       connection will be dropped immediately without calling
       endconn(). */
private:   virtual orerror<cookie_t> startconn(clientio, rpcconn &conn) = 0;
    
    /* Get a peername on with which other hosts can connect to this
     * server */
public:    peername localname() const;
    
    /* Callback from derived classes when a connection needs to be
     * torn down. */
public:    void killconn(rpcconn &conn);
    
    /* Called by the rpcserver implementation whenever a connection
       ends, once all method invocations on that connection have
       finished. */
private:   virtual void endconn(clientio, cookie_t) = 0;
    
    /* Stop accepting new incoming operations and wait for any extant
       ones to finish.  Leaves the server in an invalid state from
       which the only valid operation is the destructor.  No-op if
       we've already stopped. */
protected: void stop(clientio);

    /* Release a server, stopping it first if necessary. */
    /* Not a true destructor because it can wait for things. */
public:    void destroy(clientio);

    /* Ideally this would be private (destroy should be the only thing
       destructing these structures), but gcc doesn't allow that, for
       some reason. */
protected: virtual ~rpcserver();
};

#endif /* !RPCSERVER_H__ */
