/* The beacon client is the client half of the beacon protocol.  It is
 * responsible for sending beacon requests and receiving beacon
 * responses (including broadcast responses), and it uses these to
 * maintain a mapping from persistent slavenames to the peernames on
 * which those slaves are currently listening. */
/* XXX the beaconclientresult vs beaconclientslot distinction is
 * crazy.  Get rid of it. */
#ifndef BEACONCLIENT_H__
#define BEACONCLIENT_H__

#include "actortype.H"
#include "beacon.H"
#include "clustername.H"
#include "controlserver.H"
#include "peername.H"
#include "slavename.H"
#include "thread.H"
#include "timedelta.H"
#include "timestamp.H"
#include "tuple.H"
#include "udpsocket.H"
#include "util.H"
#include "walltime.H"
#include "wireproto.H"

class clientio;
template <typename> class orerror;
class quickcheck;

#define _beaconclientconfig(ctxt, iter0, iter1)                         \
    iter0(ctxt, 1, mand, ref, ::beaconconfig, proto)                    \
    /* What cluster are we tracking? */                                 \
    iter0(ctxt, 2, mand, ref, ::clustername, cluster)                   \
    /* Optionally filter on the type of remote hosts to track */        \
    iter0(ctxt, 3, opt, value, actortype, type)                         \
    /* Optionally filter on the slavename of remote hosts to track. */  \
    /* This is only really useful from command line tools. */           \
    iter0(ctxt, 4, opt, ref, slavename, name)                           \
    /* How long should we wait after making a query before sending */   \
    /* another one? */                                                  \
    iter0(ctxt, 5, mand, value, ::timedelta, queryinterval)             \
    /* How often should we send broadcast queries to discover new */    \
    /* peers? */                                                        \
    iter0(ctxt, 6, mand, value, ::timedelta, broadcastinterval)         \
    /* How long should we keep cached entries around for if they're */  \
    /* not being used? */                                               \
    iter1(ctxt, 7, mand, value, ::timedelta, gctimeout)
class beaconclientconfig {
    mktuplefields(beaconclientconfig, public);
public: static beaconclientconfig dflt(const clustername &,
                                       maybe<actortype> = Nothing,
                                       const maybe<slavename> & = Nothing); };
mktupleext(beaconclientconfig);

#define _beaconclientresult(ctxt, iter0, iter1)                         \
    /* Interface exposed by remote system */                            \
    iter0(ctxt, 1, mand, value, ::actortype, type)                      \
    /* Where to connect to for this server? */                          \
    iter0(ctxt, 2, mand, ref, ::peername, server)                       \
    /* Where is the beacon for this server? */                          \
    iter0(ctxt, 3, mand, ref, ::peername, beacon)                       \
    /* When did we last get a beacon response for this slave? */        \
    iter1(ctxt, 4, mand, value, ::walltime, lastbeaconresponsewall)
class beaconclientresult {
    mktuplefields(beaconclientresult, public);
};
mktupleext(beaconclientresult);

#define _beaconclientslotstatus(ctxt, iter0, iter1)                     \
    /* name of remote system */                                         \
    iter0(ctxt, 1, mand, ref, ::slavename, name)                        \
    /* Nothing if we've not gotten a beacon response yet or the */      \
    /* response details otherwise. */                                   \
    iter0(ctxt, 2, opt, ref, ::beaconclientresult, result)              \
    /* When did we last send a request to the beacon?  Nothing if */    \
    /* either we've never sent a request or if the last request we */   \
    /* sent has already received a response. */                         \
    iter0(ctxt, 3, opt, value, ::walltime, lastbeaconrequestwall)       \
    /* When we this entry last used?  We only bother to refresh things*/ \
    /* which have been used recently. */                                \
    iter0(ctxt, 4, mand, value, ::walltime, lastusedwall)               \
    /* Simple reference count for the slot.  The slot is deleted if */  \
    /* this reaches zero.  Being in the beaconclient cache list counts*/\
    /* as a reference. */                                               \
    iter1(ctxt, 5, mand, value, unsigned, refcount)
class beaconclientslotstatus {
    mktuplefields(beaconclientslotstatus, public); };
mktupleext(beaconclientslotstatus);

class beaconclientslot {
    _beaconclientslotstatus(Z, __mktuplefields, __mktuplefields);
public: maybe<timestamp> lastbeaconrequest;
public: timestamp lastused;
    /* Set if we've decided to let this slot expire rather than
     * refreshing it. */
public:  bool dead;
    /* When will the entry expire if we fail to refresh it?  Only set
     * if result is non-Nothing (and can't be in the beaconcientresult
     * itself because that's a wire type and class timestamp is only
     * meaningful host-local).*/
public: maybe<timestamp> expiry;
    /* When did we receive content of result?  Only set of result is
     * non-Nothing. */
public: maybe<timestamp> lastbeaconresponse;
    /* Protects all of our fields except pub (and mux itself).  Nests
     * inside the beaconclient cachelock.  Mutable because we acquire
     * it from beaconclient::status(), which is const. */
public:  mutable mutex_t mux;
    /* Notified whenever result goes from Nothing to non-Nothing and
     * whenever dead goes from false to true. */
public:  publisher pub;
public:  explicit beaconclientslot(const slavename &,
                                   walltime,
                                   timestamp);
public:  beaconclientslot(const slavename &,
                          const beaconclientresult &,
                          walltime,
                          timestamp,
                          timestamp);
    /* Increment the reference count.  Caller must eventually call
     * deref(). */
public:  void ref();
    /* Decrement the reference count, deleting the slot if it reaches
     * zero. */
public:  void deref();
    /* status interface */
public:  typedef beaconclientslotstatus status_t;
public:  status_t status() const; };

#define _beaconclientstatus(ctxt, iter0, iter1)                         \
    iter0(ctxt, 1, mand, ref, ::beaconclientconfig, config)             \
    iter0(ctxt, 2, mand, ref, ::list< ::beaconclientslot::status_t>, cache) \
    iter1(ctxt, 3, mand, value, int, errors)
class beaconclientstatus {
    mktuplefields(beaconclientstatus, public);
public: beaconclientstatus(const beaconclientconfig &, int);
public: ~beaconclientstatus(); };
mktupleext(beaconclientstatus);

class beaconclient : private thread {
    friend class pausedthread<beaconclient>;
    friend class thread;
    friend class iterator;
private: class controliface : public controlinterface {
    private: beaconclient *const owner;
    public:  controliface(beaconclient *, controlserver *);
    private: void getstatus(rpcservice::response *) const final;
    private: void getlistening(rpcservice::response *) const final; };
private: const beaconclientconfig config;
    /* Protects @cache.  The locks on individual entries nest inside
     * this one.  mutable because it's acquired from status(). */
private: mutable mutex_t cachelock;
    /* Never NULL.  Only a pointer because the things need to live
     * after they get removed from the list. */
private: list<beaconclientslot *> cache;
private: waitbox<void> shutdown;
    /* Notified whenever someone other that the cache thread adds
     * something to @cache. */
private: publisher newslave;
    /* Notified whenever the cache thread alters the content of the
     * cache. /*/
public:  publisher changed;
private: udpsocket listenfd;
private: udpsocket clientfd;
private: int errors;
private: int ignored;
private: maybe<controliface> _controliface;
    /* Use ::build instead */
private: beaconclient(const thread::constoken &token,
                      const beaconclientconfig &,
                      controlserver *,
                      udpsocket,
                      udpsocket);
    /* Construct a new beacon client.  Note that this does not wait
     * for the responses to its initial query to come back, so the
     * client will initially have an empty mapping.  If the @cs
     * parameter is non-NULL then we expose a status interface over
     * that control server. */
public:  static orerror<beaconclient *> build(
    const beaconclientconfig &config,
    controlserver *cs = NULL);
private: void run(clientio);
    /* Query the client's current knowledge of a particular slave,
     * returning its peername and actortype if we have one or Nothing
     * if we don't. */
public:  maybe<beaconclientresult> poll(const slavename &);
    /* Convenience function: query for a slavename, waiting as long as
     * necessary to get a result.  This is only really useful from
     * command line tools: if the remote machine has crashed, it'll
     * wait forever for it to come back, which is only really sensible
     * in a context where the user will control-C you if you're taking
     * an unreasonable amount of time. */
public:  beaconclientresult query(clientio, const slavename &);
    /* Iterator for enumerating all of the entries in the cache.  If
     * given, the actortype restricts the enumeration to only return
     * entries of a particular type.  Otherwise, all entries are
     * returned. */
    /* Note that having an extant enumerator does not prevent the contents
     * of the cache from changing.  We provide callers with the
     * following guarantees:
     *
     * 1) Anything which is in the client at the start of the
     *    enumeration and remains in it until the end will be returned
     *    during the enumeration (assuming it has the correct
     *    actortype).
     * 2) Anything returned by the enumerator was in the cache at some
     *    point between the start and end of the enumeration.
     * 3) The enumerator will never return any slave more than once.
     *
     * The caller must ensure that all iterators are released before
     * the client is destroy()ed.
     */
public:  class iterator {
        friend class beaconclient;
    private: class entry {
        public: slavename name;
        public: actortype type;
        public: peername peer;
        public: entry(const slavename &, actortype, const peername &); };
    private: list<entry> content;
        /* Never Nothing except for a brief interval in the constructor */
    private: maybe<list<entry>::const_iter> it;
    private: iterator(beaconclient *, maybe<actortype>);
    public:  const slavename &name() const;
    public:  actortype type() const;
    public:  const peername &peer() const;
    public:  bool finished() const;
    public:  void next();
    public:  ~iterator(); };
public:  iterator start(maybe<actortype> = Nothing);
    /* status interface */
public:  typedef beaconclientstatus status_t;
public:  status_t status() const;
    /* config interface */
public:  typedef beaconclientconfig config_t;
    /* Shut the client down and destroy it. */
public:  void destroy(clientio);
    /* Use destroy() instead. */
private: ~beaconclient(); };

#endif /* !BEACONCLIENT_H__ */
