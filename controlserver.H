#ifndef CONTROLSERVER_H__
#define CONTROLSERVER_H__

#include "rpcconn.H"
#include "rpcserver.H"
#include "wireproto.H"

template <typename> class orerror;
class peername;
class shutdowncode;
template <typename> class waitbox;

class controlconn;
class controlserver;

class controlinterface {
    /* All fields are protected by owner->ifacelock. */
    friend class controlconn;
private: bool inuse; /* Starts true, remains true until we're too far
                        through shutdown to accept further
                        requests. */
private: list<controlconn *> invoking; /* Inverse index for the
                                        * controlconn @invoking
                                        * list. */
private: unsigned active; /* Count of control conns which are invoking
                             us right now (which aren't in the
                             @invoking list). */
private: publisher idle; /* signalled whenever we enter a state where
                            @invoking is empty and active is zero. */
private: controlserver *const owner;

    /* Construct a new control interface and register it with the
       control server.  The interface starts accepting queries as soon
       as it is constructed. */
protected: explicit controlinterface(controlserver *cs);
    /* Add parameters to a STATUS query. */
public:    virtual void getstatus(wireproto::tx_message *msg) const = 0;
    /* Add parameters to a LISTENING query. */
public:    virtual void getlistening(wireproto::resp_message *msg) const = 0;
    /* Destruct the interface, waiting for any extant invocations of
     * getstatus() and getlistening() to finish if necessarry.  We get
     * away with waiting from a destructor because all the getstatus()
     * and getlistening() methods are guaranteed to be quick. */
public:    virtual ~controlinterface();
};

class controlconn : public rpcconn {
    friend class pausedthread<controlconn>;
    friend class controlinterface;
private: list<controlinterface *> invoking; /* All the interfaces
                                               we're about to invoke.
                                               interfaces can only be
                                               added to this list if
                                               their started field is
                                               true.  Protected by
                                               owner->ifacelock.*/
private: controlserver *const owner;
public:  controlconn(const rpcconn::rpcconntoken &tok,
                     controlserver *_owner);
public:  messageresult message(
    const wireproto::rx_message &,
    messagetoken) final;
    /* Invoke a method on the control interfaces. */
private: void invoke(const std::function<void (controlinterface *)> &);
public:  ~controlconn() {}
};

class controlserver : public rpcserver {
    friend class controlconn;
    friend class pausedthread<controlserver>;
    friend class thread;
    friend class controlinterface;
private: list<controlinterface *> ifaces; /* Protected by @ifacelock */
    /* Invoked from constructors and destructors -> must be small and
     * leaf */
private: mutex_t ifacelock;
private: waitbox<shutdowncode> &shutdown;
private: controlserver(constoken,
                       listenfd,
                       waitbox<shutdowncode> &_shutdown);
    /* Create a new control server and start serving requests.  The
       server will put a shutdown code in @s when it receives a QUIT
       message, but will continue serving incoming requests until you
       call destroy(). */
public:  static orerror<controlserver *> build(const peername &listenon,
                                               waitbox<shutdowncode> &s);
private: virtual orerror<rpcconn *> accept(socket_t);
};

#endif /* !CONTROLSERVER_H__ */
