#ifndef CONTROLSERVER_H__
#define CONTROLSERVER_H__

#include "orerror.H"
#include "waitbox.H"

class shutdowncode;

class controlserver {
    controlserver();
public:
    static orerror<controlserver *> build(waitbox<shutdowncode> *s);
    void destroy();
};

#endif /* !CONTROLSERVER_H__ */
