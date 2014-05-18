#ifndef CONTROLCLIENT_H__
#define CONTROLCLIENT_H__

#include "buffer.H"
#include "fd.H"
#include "orerror.H"
#include "wireproto.H"

class controlclient {
    controlclient() = delete;
public:
    maybe<error> send(const wireproto::tx_message &what);
    static orerror<controlclient *> connect();
    void destroy() const;
};

#endif /* !CONTROLCLIENT_H__ */
