#ifndef BEACONCLIENT_H__
#define BEACONCLIENT_H__

#include "mastersecret.H"
#include "peername.H"
#include "registrationsecret.H"
#include "timedelta.H"

template <typename> class orerror;

class beaconresult {
public: masternonce const nonce;
public: peername const slavename;
public: peername const mastername;
public: registrationsecret const secret;
public: beaconresult(const masternonce &, const peername &, const peername &,
                     const registrationsecret &);
};

class beaconclientconfig;

orerror<beaconresult> beaconclient(const beaconclientconfig &rs);

class beaconclientconfig {
    friend orerror<beaconresult> beaconclient(const beaconclientconfig &rs);
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

#endif /* !BEACONCLIENT_H__ */
