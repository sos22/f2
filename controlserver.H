#ifndef CONTROLSERVER_H__
#define CONTROLSERVER_H__

#include "rpcserver.H"

template <typename> class orerror;
class peername;
class shutdowncode;
template <typename> class waitbox;

class controlconn;

class statusinterface {
public: virtual void getstatus(
    wireproto::tx_message *msg,
    mutex_t::token /* status lock */) const = 0;
};

namespace _controlserver {
class controlserverimpl;
}

class statusregistration {
    friend class _controlserver::controlserverimpl;
private: statusinterface *what;
private: _controlserver::controlserverimpl *owner;
private: statusregistration(
    statusinterface *,
    _controlserver::controlserverimpl *);
public:  void unregister();
};

class controlserver : public rpcserver<controlconn> {
    /* Create a new control server and start serving requests.  The
       server will put a shutdown code in @s when it receives a QUIT
       message, but will continue serving incoming requests until you
       call destroy(). */
public: static orerror<controlserver *> build(const peername &listenon,
                                              waitbox<shutdowncode> &s);
    /* Release the control server interface, stopping it first if
     * necessary. */
public: virtual void destroy(clientio) = 0;
    /* Register a new status interface with the control server */
public: virtual statusregistration registeriface(statusinterface &) = 0;
};

#endif /* !CONTROLSERVER_H__ */
