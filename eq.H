/* Bits of the event queue system which are common to both client and
 * server. */
#ifndef EQ_H__
#define EQ_H__

class deserialise1;
class serialise1;
class string;

namespace fields { class field; }

namespace proto {
namespace eq {

/* Implementation detail of name and eqclient.  Can't quite hide it
 * all because the friend declarations are too much of a pain. */
class genname {
private:   unsigned long v;
protected: explicit genname(unsigned long _v) : v(_v) {}
public:    explicit genname(deserialise1 &);
public:    void serialise(serialise1 &) const;
public:    const fields::field &field() const;
public:    bool operator==(genname o) const { return v == o.v; }
public:    bool operator!=(genname o) const { return v != o.v; } };

/* The name of an event queue.  There only exist a small set of
 * pre-defined queue names; there is no mechanism for allocating them
 * dynamically (although it wouldn't be terribly hard to add such a
 * thing).  Every queue has an associated message type, and is
 * parameterised on that type for documentation purposes, but note
 * that this is not checked at any point; callers must get it
 * right. */
template <typename> class name : public genname {
public: explicit name(unsigned long _v) : genname(_v) { } };
namespace names {
extern const name<unsigned> testunsigned;
extern const name<string> teststring; }

/* Before receiving events from a queue clients must subscribe to that
 * queue. */
class subscriptionid {
private: unsigned long v;
private: explicit subscriptionid(unsigned long _v) : v(_v) {}
public:  explicit subscriptionid(deserialise1 &);
public:  void serialise(serialise1 &) const;
public:  const fields::field &field() const;
public:  static subscriptionid invent();
public:  bool operator!=(subscriptionid o) const { return v != o.v; }
public:  bool operator==(subscriptionid o) const { return v == o.v; } };

/* Events to be received by a subscriber are identified by eventids.
 * These are pretty much just monotonically increasing, dense,
 * identifiers for the messages to this subscription.  Note that
 * eventids can reset if the server crashes and recovers and that
 * there is no particular guarantee that two subscriptions which
 * receive the same message will do so with the same ID. */
class eventid {
private: unsigned long v;
public:  explicit eventid(unsigned long _v) : v(_v) {}
public:  explicit eventid(deserialise1 &);
public:  void serialise(serialise1 &) const;
public:  void operator++(int) { v++; }
public:  void operator+=(int x) { v += x; }
public:  bool operator>(eventid o) const { return v > o.v; }
public:  bool operator>=(eventid o) const { return v >= o.v; }
public:  bool operator==(eventid o) const { return v == o.v; }
public:  bool operator<=(eventid o) const { return v <= o.v; }
public:  bool operator<(eventid o) const { return v < o.v; }
public:  const fields::field &field() const; };

class tag {
private: unsigned v;
private: explicit tag(unsigned _v) : v(_v) {}
public:  explicit tag(deserialise1 &);
public:  void serialise(serialise1 &) const;
public:  bool operator==(tag o) const { return v == o.v; }
public:  bool operator!=(tag o) const { return v != o.v; }
    /* Subscribe to an event queue, giving the client some control
     * over when the server releases events.  Subscriptions need to be
     * used frequently to be kept live.  An outstanding poll counts as
     * use. Inputs: name.  Outputs: subscriptionid, eventid of the
     * next event which is to be generated. */
public:  static const tag subscribe;
    /* Extract an event from the queue.  Inputs: queue name,
     * subscriptionid, eventid.  Returns: maybe<buffer> containing
     * either the event buffer or Nothing if the event hasn't been
     * generated yet. */
public:  static const tag get;
    /* Wait until the queue produces a particular event.  Inputs:
     * queue name, subscriptionid, eventid of next expected event.
     * Returns: nothing (other than a possible error indication).
     * Doesn't return success until the input eventid is available. */
public:  static const tag wait;
    /* Hint to the server that we're finished with the event stream up
     * to and including a particular eventid and it can release those
     * events.  The server can release events without this; this just
     * makes it a bit easier on them.  The hint only takes effect if
     * *every* subscription trims the event.  Inputs: queue name,
     * subscriptionid, event id.  Outputs: None. */
public:  static const tag trim;
    /* Release a subscription.  This happens implicitly if the
     * subscription isn't poll()ed for a sufficiently long time (which
     * is usually less than a minute).  Inputs: queue name,
     * subscription id.  Outputs: None. */
public:  static const tag unsubscribe; }; } }

#endif /* !EQ_H__ */