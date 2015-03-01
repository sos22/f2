/* Bits of the event queue system which are common to both client and
 * server. */
#ifndef EQ_H__
#define EQ_H__

#include "proto.H"

class deserialise1;
template <typename> class parser;
class serialise1;
class string;

namespace fields { class field; }

namespace proto {
namespace compute { class event; }
namespace storage { class event; }
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
extern const name<string> teststring;
extern const name<proto::storage::event> storage;
extern const name<proto::compute::event> compute; }
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
 * identifiers for the messages to this subscription.  Event IDs are
 * never re-used, even if the server crashes and recovers. */
class eventid {
private: unsigned long v;
private: explicit eventid(unsigned long _v) : v(_v) {}
public:  explicit eventid(deserialise1 &);
public:  void serialise(serialise1 &) const;
public:  void operator++(int) { v++; }
public:  bool operator>(eventid o) const { return v > o.v; }
public:  bool operator>=(eventid o) const { return v >= o.v; }
public:  bool operator==(eventid o) const { return v == o.v; }
public:  bool operator<=(eventid o) const { return v <= o.v; }
public:  bool operator<(eventid o) const { return v < o.v; }
    /* Generate an initial ID for a queue.  Note that this will not be
     * the same for every queue! */
public:  static eventid initial();
    /* Advance the ID by one. */
public:  eventid succ() const { return eventid(v + 1); }
    /* Retard the ID by one. */
public:  eventid pred() const { return eventid(v - 1); }
    /* Advance the event ID and then round it up, because doing that
     * sort of thing when something interesting happens sometimes
     * makes bugs a bit more obvious. */
public:  eventid gap(unsigned sz) const;
public:  const fields::field &field() const;
public:  static const parser<eventid> &_parser(); };

class tag : public proto::tag {
private: explicit tag(unsigned char _v) : proto::tag(_v) {}
public:  explicit tag(deserialise1 &);
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

namespace fields { const field &mk(proto::eq::eventid); }

namespace parsers {
namespace eq {
const parser<proto::eq::eventid> &eventid(); } }

#endif /* !EQ_H__ */
