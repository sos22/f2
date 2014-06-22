#ifndef CONTROLSERVER_H__
#define CONTROLSERVER_H__

#include "rpcserver.H"

template <typename> class orerror;
class peername;
class shutdowncode;
template <typename> class waitbox;

class controlconn;

class controlserver : public rpcserver<controlconn *> {
    /* Create a new control server and start serving requests.  The
       server will put a shutdown code in @s when it receives a QUIT
       message, but will continue serving incoming requests until you
       call destroy(). */
public: static orerror<controlserver *> build(const peername &listenon,
                                              waitbox<shutdowncode> &s);
    /* Release the control server interface, stopping it first if
     * necessary. */
public: virtual void destroy() = 0;
};

#endif /* !CONTROLSERVER_H__ */
