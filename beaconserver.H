/* The beacon server is a thing which listens for incoming BEACON
 * requests and, if they match our configuration, replies with BEACON
 * responses.  It also sends a unilateral BEACON response to the
 * broadcast interface when it starts, just to help things get
 * started. */
#ifndef BEACONSERVER_H__
#define BEACONSERVER_H__

#include "beacon.H"
#include "clustername.H"
#include "peername.H"
#include "interfacetype.H"
#include "agentname.H"
#include "timedelta.H"
#include "thread.H"
#include "udpsocket.H"

class clientio;
class deserialise1;
class serialise1;

class beaconserverconfig {
public: const beaconconfig proto;
public: const clustername cluster;
public: const agentname name;
public: const timedelta cachetime;
public: beaconserverconfig(const beaconconfig &_proto,
                           const clustername &_cluster,
                           const agentname &_name,
                           timedelta _cachetime);
public: explicit beaconserverconfig(deserialise1 &);
public: void serialise(serialise1 &) const;
public: bool operator==(const beaconserverconfig &o) const;
public: bool operator!=(const beaconserverconfig &o) const {
    return !(*this == o); }
public: static const ::parser<beaconserverconfig> &parser();
public: const fields::field &field() const;
public: static beaconserverconfig dflt(const clustername &,
                                       const agentname &); };

class beaconserver : private thread {
    friend class thread;
    friend class pausedthread<beaconserver>;

private: beaconserver() = delete;
private: beaconserver(const beaconserver &) = delete;
private: void operator=(const beaconserver &) = delete;

private: beaconserverconfig config;
private: const list<interfacetype> advertisetype;
private: const peername::port advertiseport;
private: udpsocket listenfd;
private: udpsocket clientfd;
public:  udpsocket &__test_clientfd() { return clientfd; }
private: waitbox<void> shutdown;
private: unsigned errors; /* total number of errors, ever */
private: unsigned ignored; /* total number of ignored requests,
                            * excluding errors, ever */

    /* Construct a new beacon server. */
public:  static orerror<beaconserver *> build(const beaconserverconfig &,
                                              const list<interfacetype> &type,
                                              peername::port port);
    /* Use build() */
private: beaconserver(thread::constoken,
                      const beaconserverconfig &config,
                      const list<interfacetype> &type,
                      peername::port port,
                      udpsocket listenfd,
                      udpsocket clientfd);
private: void run(clientio);

    /* Shut down a beacon server. */
public: void destroy(clientio);

    /* Use destroy() instead */
private: ~beaconserver();

    /* Debug interface */
public: void status();

public: typedef beaconserverconfig config_t; };

#endif /* !BEACONSERVER_H__ */
