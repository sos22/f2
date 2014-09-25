/* The beacon server is a thing which listens for incoming BEACON
 * requests and, if they match our configuration, replies with BEACON
 * responses.  It also sends a unilateral BEACON response to the
 * broadcast interface when it starts, just to help things get
 * started. */
#ifndef BEACONSERVER_H__
#define BEACONSERVER_H__

#include "actortype.H"
#include "beacon.H"
#include "clustername.H"
#include "controlserver.H"
#include "peername.H"
#include "slavename.H"
#include "timedelta.H"
#include "tuple.H"
#include "udpsocket.H"
#include "wireproto.H"

class clientio;

#define _beaconserverconfig(ctxt, iter0, iter1)                         \
    iter0(ctxt, 1, ref, ::beaconconfig, proto)                          \
    /* What cluster are we broadcasting our beacon messages into? */    \
    iter0(ctxt, 2, ref, clustername, cluster)                           \
    /* Who are we advertising? */                                       \
    iter0(ctxt, 3, ref, slavename, name)                                \
    /* How long may clients cache our responses for?  Note that this */ \
    /* will be measured from the point they receive the response, and */ \
    /* not the point at which we send it i.e. the actual liveness time */ \
    /* is this plus one network delay. */                               \
    iter1(ctxt, 4, value, timedelta, cachetime)
class beaconserverconfig {
    mktuplefields(beaconserverconfig, public); };
mktupleext(beaconserverconfig);

#define _beaconserverstatus(ctxt, iter0, iter1)                         \
    iter0(ctxt, 1, ref, ::beaconserverconfig, config)                   \
    /* How many errors have we encountered, ever? */                    \
    iter0(ctxt, 2, value, unsigned, errors)                             \
    /* How many requests have we ignored due to filter mismatches, */   \
    /* ever? */                                                         \
    iter0(ctxt, 3, value, unsigned, ignored)                            \
    /* What type of interface are we advertising? */                    \
    iter0(ctxt, 4, value, ::actortype, type)                            \
    /* On what port is it exposed? */                                   \
    iter1(ctxt, 5, ref, peernameport, port)
class beaconserverstatus {
    mktuplefields(beaconserverstatus, public); };
mktupleext(beaconserverstatus);

class beaconserver : private thread {
    friend class thread;
    friend class pausedthread<beaconserver>;

private: class controliface : public controlinterface {
    private: beaconserver *owner;
    public:  controliface(beaconserver *_owner, controlserver *cs);
    public:  void getstatus(wireproto::tx_message *) const;
    public:  void getlistening(wireproto::resp_message *) const;
    };

private: beaconserver() = delete;
private: beaconserver(const beaconserver &) = delete;
private: void operator=(const beaconserver &) = delete;

private: controliface controliface_;
private: beaconserverconfig config;
private: const actortype advertisetype;
private: const peername::port advertiseport;
private: udpsocket listenfd;
private: udpsocket clientfd;
private: waitbox<void> shutdown;
private: unsigned errors; /* total number of errors, ever */
private: unsigned ignored; /* total number of ignored requests,
                            * excluding errors, ever */

    /* Construct a new beacon server.  We expose listening and status
     * interfaces over the controlserver cs. */
public:  static orerror<beaconserver *> build(const beaconserverconfig &,
                                              actortype type,
                                              peername::port port,
                                              controlserver *cs);
    /* Use build() */
private: beaconserver(thread::constoken,
                      const beaconserverconfig &config,
                      actortype type,
                      peername::port port,
                      controlserver *cs,
                      udpsocket listenfd,
                      udpsocket clientfd);
private: void run(clientio);

    /* Shut down a beacon server. */
public: void destroy(clientio);

    /* Use destroy() instead */
private: ~beaconserver();

public: typedef beaconserverstatus status_t;
public: status_t status() const;

public: typedef beaconserverconfig config_t; };

#endif /* !BEACONSERVER_H__ */
