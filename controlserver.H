#ifndef CONTROLSERVER_H__
#define CONTROLSERVER_H__

#include "orerror.H"
#include "waitbox.H"

class shutdowncode;

class controlserver {
    controlserver() = delete;
public:
    /* Create a new control server and start serving requests.  The
       server will stop server requests as soon as somebody puts a
       shutdown code in @s, but you must still call the ::destroy()
       method. */
    static orerror<controlserver *> build(waitbox<shutdowncode> *s);
    /* Release the control server interface, stopping it first if
     * necessary. */
    void destroy();
};

#endif /* !CONTROLSERVER_H__ */
