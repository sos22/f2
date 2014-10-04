#ifndef CONTROLSERVER_H__
#define CONTROLSERVER_H__

#include "mutex.H"
#include "pubsub.H"
#include "rpcservice.H"

template <typename> class orerror;
class peername;
class shutdowncode;
template <typename> class waitbox;

class controlserver;
class controlinvoker;

class controlinterface {
    /* All fields are protected by owner->ifacelock. */
    friend class controlserver;
private: controlserver *const owner;
    /* All the places we're about to get invoked, before they
     * increment the refcount.  Removing something from the list
     * prevents it from bumping the refcount. */
private: list<controlinvoker *> pendinginvoke;
    /* Count of things other than pendinginvoke which are about to
     * call into this interface. */
private: unsigned refcount;
    /* Set whenever we enter a state with pendinginvoke empty and
     * refcount zero. */
private: publisher idle;
    /* Has start() been called yet? */
private: bool started;
    /* Construct a new control interface and register it with the
     * control server.  You must call ->start() when the interface is
     * ready to start accepting messages. */
protected: explicit controlinterface(controlserver *cs);
    /* Start accepting queries on this interface.  This must be called
     * precisely once for every instance of the class. */
protected: void start();
    /* Add parameters to a STATUS query. */
public:    virtual void getstatus(rpcservice::response *msg) const = 0;
    /* Add parameters to a LISTENING query. */
public:    virtual void getlistening(rpcservice::response *msg) const = 0;
    /* Destruct the interface, waiting for any extant invocations of
     * getstatus() and getlistening() to finish if necessarry.  We get
     * away with waiting from a destructor because all the getstatus()
     * and getlistening() methods are guaranteed to be quick. */
public:    virtual ~controlinterface();
};

class controlserver : public rpcservice {
    friend class controlinterface;
    friend class rpcservice;
private: list<controlinterface *> ifaces; /* Protected by @ifacelock */
    /* Invoked from constructors and destructors -> must be small and
     * leaf */
private: mutex_t ifacelock;
    /* Waitbox which we fill with a shutdown code when we receive a
     * shutdown request from a remote peer. */
private: waitbox<shutdowncode> &shutdown;
    /* Protects shutdown */
private: mutex_t shutdownlock;
private: controlserver(const constoken &,
                       waitbox<shutdowncode> &_shutdown);
    /* Map over all registered interfaces in a safe way. */
private: void invoke(const std::function<void (controlinterface *)> &);
    /* Create a new control server and start serving requests.  The
       server will put a shutdown code in @s when it receives a QUIT
       message, but will continue serving incoming requests until you
       call destroy(). */
public:  static orerror<controlserver *> build(clientio,
                                               const peername &listenon,
                                               waitbox<shutdowncode> &s);
private: void call(const wireproto::rx_message &, response *) final;
};

#endif /* !CONTROLSERVER_H__ */
