#ifndef MASTERCONFIG_H__
#define MASTERCONFIG_H__

#include "frequency.H"
#include "peername.H"
#include "ratelimiter.H"
#include "registrationsecret.H"
#include "rpcconn.H"
#include "string.H"

class masterconfig;
class quickcheck;

class masterconfig {
private: masterconfig() = delete;
public:  peername controlsock;
public:  registrationsecret rs;
public:  peername listenon;
public:  peername::port beaconport;
public:  ratelimiterconfig beaconlimit;
public:  rpcconnconfig connconfig;
public:  masterconfig(const peername &,
                      const registrationsecret &,
                      const peername &,
                      const peername::port &,
                      const ratelimiterconfig &,
                      const rpcconnconfig &);
public:  masterconfig(const registrationsecret &rs);
public:  masterconfig(const quickcheck &q);
public:  masterconfig _controlsock(const peername &s) const;
public:  masterconfig _rs(const registrationsecret &rs) const;
public:  masterconfig _listenon(const peername &) const;
public:  masterconfig _beaconport(const peername::port &) const;
public:  masterconfig _beaconlimit(const ratelimiterconfig &) const;
public:  masterconfig _connconfig(const rpcconnconfig &) const;
public:  bool operator==(const masterconfig &o) const;
};

template <typename> class parser;
namespace parsers {
const parser<masterconfig> &_masterconfig();
}

namespace fields {
class field;
const field &mk(const masterconfig &);
}

namespace tests {
void _masterconfig();
}

#endif /* !MASTERCONFIG_H__ */
