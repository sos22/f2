/* The beacon server is a thing which listens for incoming BEACON
 * requests and, if they match our configuration, replies with BEACON
 * responses.  It also sends a unilateral BEACON response to the
 * broadcast interface when it starts, just to help things get
 * started. */
#ifndef BEACONSERVER_H__
#define BEACONSERVER_H__

#include "beacon.H"
#include "beacontest.H"
#include "clustername.H"
#include "peername.H"
#include "interfacetype.H"
#include "slavename.H"
#include "timedelta.H"
#include "thread.H"
#include "tuple.H"
#include "udpsocket.H"
#include "wireproto.H"

class clientio;

#define _beaconserverconfig(ctxt, iter0, iter1)                         \
    iter0(ctxt, 1, mand, ref, ::beaconconfig, proto)                    \
    /* What cluster are we broadcasting our beacon messages into? */    \
    iter0(ctxt, 2, mand, ref, clustername, cluster)                     \
    /* Who are we advertising? */                                       \
    iter0(ctxt, 3, mand, ref, slavename, name)                          \
    /* How long may clients cache our responses for?  Note that this */ \
    /* will be measured from the point they receive the response, and */ \
    /* not the point at which we send it i.e. the actual liveness time */ \
    /* is this plus one network delay. */                               \
    iter1(ctxt, 4, mand, value, timedelta, cachetime)
class beaconserverconfig {
    mktuplefields(beaconserverconfig, public);
public: static beaconserverconfig dflt(const clustername &,
                                       const slavename &); };
mktupleext(beaconserverconfig);

class beaconserver : private thread {
    friend class thread;
    friend class pausedthread<beaconserver>;
    friend void tests::beacon();

private: beaconserver() = delete;
private: beaconserver(const beaconserver &) = delete;
private: void operator=(const beaconserver &) = delete;

private: beaconserverconfig config;
private: const list<interfacetype> advertisetype;
private: const peername::port advertiseport;
private: udpsocket listenfd;
private: udpsocket clientfd;
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

public: typedef beaconserverconfig config_t; };

#endif /* !BEACONSERVER_H__ */
