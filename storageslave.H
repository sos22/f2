#ifndef STORAGESLAVE_H__
#define STORAGESLAVE_H__

#include "controlserver.H"
#include "proto.H"
#include "rpcinterface.H"

class error;
template <typename> class maybe;
template <typename> class orerror;
class registrationsecret;
class rpcconn;

class storageslave {
    class statusiface : public rpcinterface<controlconn *> {
    private: storageslave *owner;
    public:  statusiface(storageslave *_owner)
        : rpcinterface(proto::BEACONSLAVESTATUS::tag),
          owner(_owner)
            {}
    private: maybe<error> message(
        const wireproto::rx_message &,
        controlconn *,
        buffer &); };
    statusiface statusinterface;
    rpcregistration<controlconn *> *controlregistration;
    rpcconn *masterconn;
    storageslave(controlserver *);
    maybe<error> connect(clientio, const registrationsecret &);
public:
    static orerror<storageslave *> build(
        clientio io,
        const registrationsecret &rs,
        controlserver *);
    void destroy() const;
};

#endif /* !STORAGESLAVE_H__ */
