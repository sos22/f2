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
template <typename, typename> class pair;
template <typename> class orerror;
class quickcheck;

class beaconclientslot;

class beaconclientconfig {
    /* What cluster are we tracking? */
private: clustername _cluster;
public:  const clustername &cluster() const { return _cluster; }
    /* Optionally filter on the type of remote hosts to track */
private: maybe<actortype> _type;
public:  maybe<actortype> type() const { return _type; }
    /* Optionally filter on the slavename of remote hosts to track.
     * This is only really useful from command line tools. */
private: maybe<slavename> _name;
    /* Beacon ports to use. */
private: beaconconfig _proto;
public:  const beaconconfig &proto() const { return _proto; }
    /* How long should we wait after making a query before sending
     * another one? */
private: timedelta _queryinterval;
public:  timedelta queryinterval() const { return _queryinterval; }
    /* How often should we send broadcast queries to discover new
     * peers? */
private: timedelta _broadcastinterval;
public:  timedelta broadcastinterval() const { return _broadcastinterval; }
public:  const maybe<slavename> &name() const { return _name; }

public:  beaconclientconfig(quickcheck);
    /* The type system guarantees that some fields can be overridden
     * without an error, so if you're only overriding those you can
     * use an ordinary constructor.  For others you might get an
     * error, so you need to use a mk() method with an orerror<>
     * return. */
public:  beaconclientconfig(
    const clustername &,
    maybe<actortype> = Nothing,
    const maybe<slavename> & = Nothing,
    const beaconconfig & = beaconconfig::dflt);
public:  static orerror<beaconclientconfig> mk(
    const clustername &,
    maybe<actortype> = Nothing,
    const maybe<slavename> & = Nothing,
    const beaconconfig & = beaconconfig::dflt,
    timedelta = timedelta::seconds(1),
    timedelta = timedelta::minutes(2));
private: beaconclientconfig(
    const clustername &,
    maybe<actortype>,
    const maybe<slavename> &,
    const beaconconfig &,
    timedelta,
    timedelta);
public:  bool operator==(const beaconclientconfig &o) const;
public:  bool operator!=(const beaconclientconfig &o) const;
public:  const fields::field &field() const; };

namespace fields {
class field;
const field &mk(const beaconclientconfig &); }
template <typename t> class parser;
namespace parsers {
const parser<beaconclientconfig> &__beaconclientconfig(); }

namespace tests { void beacon(); }

class beaconclient : private thread {
    friend class pausedthread<beaconclient>;
    friend class thread;
    friend class iterator;
    friend void tests::beacon();
private: class controliface : public controlinterface {
    private: beaconclient *const owner;
    public:  controliface(beaconclient *, controlserver *);
    private: void getstatus() const final; };
private: const beaconclientconfig config;
    /* Protects @cache.  Leaf lock.  mutable because it's acquired
     * from status(). */
private: mutable mutex_t cachelock;
private: list<beaconclientslot> cache;
private: waitbox<void> shutdown;
    /* Notified whenever a new thing gets added to the cache. */
private: publisher _changed;
    /* Used to listen to broadcast messages. */
private: udpsocket listenfd;
    /* Used to listen for directed messages and to send all
     * responses. */
private: udpsocket clientfd;
    /* How many errors have we suffered, ever? */
private: unsigned errors;
    /* How many requests have we ignored because of the filter,
     * ever? */
private: unsigned ignored;
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
private: void sendbroadcast();
private: void handletimeouts(mutex_t::token);
public:  class result {
        friend class beaconclient;
    private: result(const peername &, actortype);
    public:  const peername name;
    public:  const actortype type; };
    /* Query the client's current knowledge of a particular slave,
     * returning its peername and actortype if we have one or Nothing
     * if we don't. */
public:  maybe<result> poll(const slavename &);
    /* Convenience function: query for a slavename, waiting as long as
     * necessary to get a result.  This is only really useful from
     * command line tools: if the remote machine has crashed, it'll
     * wait forever for it to come back, which is only really sensible
     * in a context where the user will control-C you if you're taking
     * an unreasonable amount of time. */
public:  result query(clientio, const slavename &);
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
    /* The actual implementation provides much stronger properties
     * than that, but callers should try not to rely on them. */
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

    /* Notified whenever the set of entries reported by the iterator
     * increases and whenever any result from poll() goes from Nothing
     * to something. */
public:  const publisher &changed() const;

    /* status interface */
public:  void status() const;

    /* config interface */
public:  typedef beaconclientconfig config_t;

    /* Shut the client down and destroy it. */
public:  void destroy(clientio);

    /* Use destroy() instead. */
private: ~beaconclient(); };

#endif /* !BEACONCLIENT_H__ */
