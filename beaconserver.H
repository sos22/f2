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
#include "thread.H"
#include "udpsocket.H"

class frequency;

class beaconserver {
    friend class statusiface;
    friend class configureiface;

    class statusiface : public controliface {
        beaconserver *owner;
        maybe<error> controlmessage(const wireproto::rx_message &,
                                    buffer &);
        statusiface() = delete;
        statusiface(const statusiface &) = delete;
        void operator=(const statusiface &) = delete;
    public:
        statusiface(beaconserver *_owner);
    };
    class configureiface : public controliface {
        beaconserver *owner;
        maybe<error> controlmessage(const wireproto::rx_message &,
                                    buffer &);
        configureiface() = delete;
        configureiface(const configureiface &) = delete;
        void operator=(const configureiface &) = delete;
    public:
        configureiface(beaconserver *_owner);
    };

    class listenthreadclass : public threadfn {
        beaconserver *owner;
        listenthreadclass() = delete;
        listenthreadclass(const listenthreadclass &) = delete;
        void operator=(const listenthreadclass &) = delete;
        void run();
    public:
        listenthreadclass(beaconserver *);
    };

    statusiface statusinterface;
    configureiface configureinterface;
    controlserver::iface controlregistration;
    registrationsecret secret;
    peername mastername;
    mastersecret mastersecret_;
    ratelimiter limiter;
    listenthreadclass listenthreadfn;
    thread *listenthread;
    udpsocket listenfd;
    waitbox<bool> *shutdown;

    unsigned errors; /* total number of errors, ever */
    unsigned rx; /* total number of packets received, ever */

    beaconserver(const registrationsecret &rs,
                 frequency max_responses,
                 const controlserver &cs,
                 const peername &mastername,
                 const mastersecret &);

    beaconserver() = delete;
    beaconserver(const beaconserver &) = delete;
    void operator=(const beaconserver &) = delete;

    ~beaconserver();
    maybe<error> listen();
public:
    static orerror<beaconserver *> build(const registrationsecret &,
                                         frequency max_responses,
                                         const controlserver &,
                                         const peername &mastername,
                                         const mastersecret &_mastersecret);
    void destroy();
};

#endif /* !BEACONSERVER_H__ */
