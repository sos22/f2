#ifndef BEACONCLIENT_H__
#define BEACONCLIENT_H__

#include "mastersecret.H"
#include "peername.H"
#include "registrationsecret.H"

template <typename> class orerror;

class beaconresult {
public: masternonce const nonce;
public: peername const slavename;
public: peername const mastername;
public: registrationsecret const secret;
public: beaconresult(const masternonce &, const peername &, const peername &,
                     const registrationsecret &);
};

orerror<beaconresult> beaconclient(const registrationsecret &rs);

#endif /* !BEACONCLIENT_H__ */
