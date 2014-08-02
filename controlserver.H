#ifndef CONTROLSERVER_H__
#define CONTROLSERVER_H__

#include "rpcconn.H"
#include "rpcserver.H"

template <typename> class orerror;
class peername;
class shutdowncode;
template <typename> class waitbox;

class controlconn;
class controlserver;

class statusinterface {
    friend class controlconn;
    /* The active list contains interfaces which we're about collect
     * status for. */
private: struct list {
    list *next;
    list *prev;
    list()
        : next(NULL),
          prev(NULL) {}
};
private: list active;
private: bool started; /* True if we're started() and not stopped() */
private: bool running; /* True if the interface is currently being invoked */
private: publisher idle; /* signalled whenever running goes true->false */
private: controlserver *const owner;

    /* Construct a new status interface and register it with the
       control server.  The interface is initially stopped and will
       not start accepting requests until ::start() is called. */
protected: statusinterface(controlserver *);
    /* Start accepting new requests on this interface.  Once this is
       called, new requests can come in to ::getstatus() at any time
       until ::stop() is called. */
public:    void start();
    /* Add status parameters to a TX message. */
public:    virtual void getstatus(wireproto::tx_message *msg) const = 0;
    /* Stop calling ::getstatus() on this interface, waiting for any
       extant invocations to finish before returning.  The interface
       will remain stopped until start() is called. */
public:    void stop();
    /* Destruct the interface.  The interface must be stopped before
     * this is called. */
public:    virtual ~statusinterface();
};

class controlconn : public rpcconn {
    friend class pausedthread<controlconn>;
private: controlserver *const owner;
public:  controlconn(thread2::constoken tok,
                     socket_t _sock,
                     const rpcconnauth &_auth,
                     const peername &_peer,
                     controlserver *_owner);
public:  virtual messageresult message(const wireproto::rx_message &);
public:  virtual ~controlconn() {}
};

class controlserver : public rpcserver {
    friend class controlconn;
    friend class statusinterface;
private: list<statusinterface *> statusifaces;
private: mutex_t statuslock;
private: waitbox<shutdowncode> &shutdown;
private: controlserver(waitbox<shutdowncode> &_shutdown);
    /* Create a new control server and start serving requests.  The
       server will put a shutdown code in @s when it receives a QUIT
       message, but will continue serving incoming requests until you
       call destroy(). */
public:  static orerror<controlserver *> build(const peername &listenon,
                                               waitbox<shutdowncode> &s);
private: virtual orerror<rpcconn *> accept(socket_t);
};

#endif /* !CONTROLSERVER_H__ */
