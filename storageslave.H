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
    class statusiface : public rpcinterface {
    private: storageslave *owner;
    public:  statusiface(storageslave *_owner)
        : rpcinterface(proto::BEACONSLAVESTATUS::tag),
          owner(_owner)
            {}
    private: maybe<error> message(
        const wireproto::rx_message &,
        rpcconn &,
        buffer &); };
    statusiface statusinterface;
    rpcregistration *controlregistration;
    rpcconn *masterconn;
    storageslave(controlserver *);
    maybe<error> connect(const registrationsecret &);
public:
    static orerror<storageslave *> build(
        const registrationsecret &rs,
        controlserver *);
    void destroy() const;
};

#endif /* !STORAGESLAVE_H__ */
