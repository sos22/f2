/* The beacon server is a thing which listens for broadcast slave
   HELLO messages and tells them where the master is.  It doesn't
   do anything else, so e.g. it doesn't track who it*/
#ifndef BEACONSERVER_H__
#define BEACONSERVER_H__

#include "controlserver.H"
#include "mastersecret.H"
#include "peername.H"
#include "quickcheck.H"
#include "ratelimiter.H"
#include "registrationsecret.H"
#include "rpcconn.H"
#include "rpcinterface.H"
#include "thread.H"
#include "udpsocket.H"

class frequency;

class beaconstatus {
WIREPROTO_TYPE(beaconstatus);
public: registrationsecret secret;
public: ratelimiter_status limiter;
public: unsigned errors;
public: unsigned rx;
public: beaconstatus(const registrationsecret &_secret,
                     const ratelimiter_status &_limiter,
                     unsigned _errors,
                     unsigned _rx)
    : secret(_secret),
      limiter(_limiter),
      errors(_errors),
      rx(_rx) {}
public: beaconstatus(const quickcheck &q)
    : secret(q),
      limiter(q),
      errors(q),
      rx(q) {}
public: bool operator==(const beaconstatus &o) const {
    return secret == o.secret &&
        limiter == o.limiter &&
        errors == o.errors &&
        rx == o.rx; }
};

namespace fields {
const field &mk(const beaconstatus &);
}

class beaconserverconfig {
    friend class beaconserver;
#define mkfield(type, name)                                     \
    private: type name ##_;                                     \
public:  beaconserverconfig name(const type &_ ## name) {       \
    beaconserverconfig res(*this);                              \
    res.name ##_ = _ ## name;                                   \
    return res; }
    mkfield(peername, coordinator)
    mkfield(frequency, maxresponses)
    mkfield(mastersecret, ms)
    mkfield(peername::port, port)
    mkfield(registrationsecret, rs)
#undef mkfield
public: beaconserverconfig(const registrationsecret &_rs,
                           const peername &_coordinator,
                           frequency _maxresponses,
                           const mastersecret &_ms,
                           peername::port _port)
    : coordinator_(_coordinator),
      maxresponses_(_maxresponses),
      ms_(_ms),
      port_(_port),
      rs_(_rs) {} };

class beaconserver : private thread {
    friend class thread;
    friend class pausedthread<beaconserver>;
    friend class statusiface;

    class statusiface : public statusinterface {
    private: beaconserver *owner;
    public:  statusiface(beaconserver *_owner, controlserver *cs);
    public:  void getstatus(wireproto::tx_message *) const;
    public:  void getlistening(wireproto::resp_message *) const;
    };

public:  statusiface statusiface_;
    registrationsecret secret;
    peername mastername;
    mastersecret mastersecret_;
    ratelimiter limiter;
    udpsocket listenfd;
    waitbox<bool> shutdown;

    unsigned errors; /* total number of errors, ever */
    unsigned rx; /* total number of packets received, ever */

    beaconserver(thread::constoken,
                 const beaconserverconfig &config,
                 controlserver *cs,
                 udpsocket listenfd);

    void run(clientio);

    beaconserver() = delete;
    beaconserver(const beaconserver &) = delete;
    void operator=(const beaconserver &) = delete;
public:
    static orerror<beaconserver *> build(const beaconserverconfig &,
                                         controlserver *);
    void destroy(clientio);

public: typedef beaconstatus status_t;
public: status_t status() const;
};

namespace tests {
template <typename> class event;
extern event<udpsocket> beaconserverreceive;
}

#endif /* !BEACONSERVER_H__ */
