#ifndef BEACONCLIENT_H__
#define BEACONCLIENT_H__

#include "mastersecret.H"
#include "peername.H"
#include "registrationsecret.H"
#include "timedelta.H"

class clientio;
template <typename> class orerror;

class beaconresult {
public: masternonce const nonce;
public: peername const connectingname;
public: peername const mastername;
public: registrationsecret const secret;
public: beaconresult(const masternonce &, const peername &, const peername &,
                     const registrationsecret &);
};

class beaconclientconfig;

orerror<beaconresult> beaconclient(clientio, const beaconclientconfig &rs);

class beaconclientconfig {
    friend orerror<beaconresult> beaconclient(clientio,
                                              const beaconclientconfig &rs);
#define mkparam(type, name)                                             \
private: type name ##_;                                                 \
public: beaconclientconfig name(const type &what) const {               \
    beaconclientconfig res(*this);                                      \
    res.name ##_ = what;                                                \
    return res; }
    mkparam(registrationsecret, rs);
    mkparam(timedelta, retryinterval);
    mkparam(maybe<int>, retrylimit);
    mkparam(peername::port, port);
#undef mkparam
public: beaconclientconfig(const registrationsecret &_rs)
    : rs_(_rs),
      retryinterval_(timedelta::seconds(1)),
      retrylimit_(Nothing),
      port_(9009) {} };

class buffer;
template <typename, typename> class pair;
class fd_t;
class nonce;
class udpsocket;
namespace tests {
template <typename> class event;
extern event< ::pair< ::fd_t, ::buffer *> > beaconclientreadytosend;
extern event< ::pair< ::udpsocket, ::nonce> > beaconclientreceiving;
}

#endif /* !BEACONCLIENT_H__ */
