/* The beacon server is a thing which listens for broadcast slave
   HELLO messages and tells them where the master is.  It doesn't
   do anything else, so e.g. it doesn't track who it*/
#ifndef BEACONSERVER_H__
#define BEACONSERVER_H__

#include "controlserver.H"
#include "mastersecret.H"
#include "peername.H"
#include "ratelimiter.H"
#include "registrationsecret.H"
#include "rpcconn.H"
#include "rpcinterface.H"
#include "thread.H"
#include "udpsocket.H"

class frequency;

class beaconstatus {
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
WIREPROTO_TYPE(beaconstatus);
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
    mkfield(registrationsecret, rs)
    mkfield(frequency, maxresponses)
    mkfield(peername, coordinator)
    mkfield(mastersecret, ms)
#undef mkfield
public: beaconserverconfig(const registrationsecret &_rs,
                           const peername &_coordinator,
                           const mastersecret &_ms)
    : rs_(_rs),
      maxresponses_(frequency::hz(10)),
      coordinator_(_coordinator),
      ms_(_ms) {} };

class beaconserver {
    friend class statusiface;
    friend class configureiface;

    class statusiface : public statusinterface {
    private: beaconserver *owner;
    public:  statusiface(beaconserver *_owner);
    private: void getstatus(wireproto::tx_message *, mutex_t::token) const;
    };

    class listenthreadclass : public threadfn {
        beaconserver *owner;
        listenthreadclass() = delete;
        listenthreadclass(const listenthreadclass &) = delete;
        void operator=(const listenthreadclass &) = delete;
        void run(clientio);
    public:
        listenthreadclass(beaconserver *);
    };

    statusiface statusiface_;
    statusregistration statusregistration_;
    registrationsecret secret;
    peername mastername;
    mastersecret mastersecret_;
    ratelimiter limiter;
    listenthreadclass listenthreadfn;
    thread *listenthread;
    udpsocket listenfd;
    waitbox<bool> shutdown;

    unsigned errors; /* total number of errors, ever */
    unsigned rx; /* total number of packets received, ever */

    beaconserver(const beaconserverconfig &config,
                 controlserver *cs);

    beaconserver() = delete;
    beaconserver(const beaconserver &) = delete;
    void operator=(const beaconserver &) = delete;

    ~beaconserver();
    maybe<error> listen();
public:
    static orerror<beaconserver *> build(const beaconserverconfig &,
                                         controlserver *);
    void destroy(clientio);

public: typedef beaconstatus status_t;
public: status_t status() const;
};

#endif /* !BEACONSERVER_H__ */
