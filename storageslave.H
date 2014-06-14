#ifndef STORAGESLAVE_H__
#define STORAGESLAVE_H__

#include "controlserver.H"
#include "proto.H"

class error;
template <typename> class maybe;
template <typename> class orerror;
class registrationsecret;
class rpcconn;

class storageslave {
    class statusiface : public controliface {
        storageslave *owner;
    public:
        statusiface(storageslave *_owner)
            : controliface(proto::BEACONSLAVESTATUS::tag),
              owner(_owner)
            {}
        maybe<error> controlmessage(const wireproto::rx_message &, buffer &);
    };
    statusiface statusinterface;
    controlserver::iface controlregistration;
    rpcconn *masterconn;
    storageslave(const controlserver &);
    maybe<error> connect(const registrationsecret &);
public:
    static orerror<storageslave *> build(
        const registrationsecret &rs,
        const controlserver &);
    void destroy() const;
};

#endif /* !STORAGESLAVE_H__ */
