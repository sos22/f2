#ifndef STORAGESLAVE_H__
#define STORAGESLAVE_H__

#include "controlserver.H"
#include "proto.H"
#include "rpcinterface.H"
#include "rpcservice.H"

class error;
template <typename> class maybe;
template <typename> class orerror;
class registrationsecret;
class rpcconn;
class storageslavectxt;

class storageslave {
private: class statusiface : public rpcinterface<controlconn> {
    private: storageslave *owner;
    public:  statusiface(storageslave *_owner)
        : rpcinterface(proto::BEACONSLAVESTATUS::tag),
          owner(_owner)
            {}
    private: messageresult message(
        const wireproto::rx_message &,
        controlconn *); };
private: class pingiface : public rpcinterface<storageslavectxt> {
    public:  pingiface()
        : rpcinterface(proto::PING::tag) {}
    private: messageresult message(
        const wireproto::rx_message &,
        storageslavectxt *); };
private: pingiface pinginterface;
private: statusiface statusinterface;
private: rpcregistration<controlconn> *controlregistration;
private: rpcservice<storageslavectxt> *const service;
private: rpcregistration<storageslavectxt> *const serviceregistration;
private: rpcconnthread<storageslavectxt> *masterconn;
private: storageslave(controlserver *);
private: maybe<error> connect(clientio, const registrationsecret &);
public:  static orerror<storageslave *> build(
    clientio io,
    const registrationsecret &rs,
    controlserver *);
public:  void destroy() const;
};

#endif /* !STORAGESLAVE_H__ */
