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
public: type name ##_;                                                 \
public: beaconclientconfig name(const type &what) const {               \
    beaconclientconfig res(*this);                                      \
    res.name ##_ = what;                                                \
    return res; }
    mkparam(registrationsecret, rs);
    mkparam(timedelta, retryinterval);
    mkparam(maybe<int>, retrylimit);
    mkparam(peername::port, port);
#undef mkparam
public: beaconclientconfig(const registrationsecret &_rs,
                           const timedelta &_retryinterval,
                           const maybe<int> &_retrylimit,
                           const peername::port &_port)
    : rs_(_rs),
      retryinterval_(_retryinterval),
      retrylimit_(_retrylimit),
      port_(_port) {}
public: explicit beaconclientconfig(const registrationsecret &_rs)
    : rs_(_rs),
      retryinterval_(timedelta::seconds(1)),
      retrylimit_(Nothing),
      port_(9009) {}
public: explicit beaconclientconfig(const quickcheck &q);

public: bool operator==(const beaconclientconfig &o) const; };

namespace fields {
class field;
const field &mk(const beaconclientconfig &);
}

template <typename> class parser;
namespace parsers {
const parser<beaconclientconfig> &_beaconclientconfig();
}

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
