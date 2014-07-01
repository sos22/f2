#ifndef CONTROLSERVER_H__
#define CONTROLSERVER_H__

#include "rpcserver.H"

template <typename> class orerror;
class peername;
class shutdowncode;
template <typename> class waitbox;

class controlconn;
class controlserver;

class statusinterface {
public: virtual void getstatus(
    wireproto::tx_message *msg,
    mutex_t::token /* status lock */) const = 0;
};

class statusregistration {
    friend class controlserver;
private: statusinterface *what;
private: controlserver *owner;
private: statusregistration(
    statusinterface *,
    controlserver *);
public:  void unregister();
};

class controlserver : public rpcserver<controlconn> {
    friend class controlconn;
    friend class statusregistration;
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
private: orerror<controlconn *> accept(socket_t);
    /* Register a new status interface with the control server */
public:  statusregistration registeriface(statusinterface &);
};

#endif /* !CONTROLSERVER_H__ */
