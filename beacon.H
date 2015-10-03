#ifndef BEACON_H__
#define BEACON_H__

#include "clustername.H"
#include "interfacetype.H"
#include "list.H"
#include "peername.H"
#include "maybe.H"
#include "agentname.H"
#include "timedelta.H"
#include "version.H"

/* Fields which a user might want to change which relate to the beacon
 * protocol. */
class beaconconfig {
private: peernameport _reqport;
private: peernameport _respport;
public:  beaconconfig(const peernameport &reqport,const peernameport &respport);
public:  beaconconfig(deserialise1 &);
public:  peernameport reqport() const { return _reqport; }
public:  peernameport respport() const { return _respport; }
public:  bool privileged() const {
    return reqport().privileged() || respport().privileged(); }
public:  void serialise(serialise1 &) const;
public:  bool operator==(const beaconconfig &) const;
public:  const fields::field &field() const;
public:  static const ::parser<beaconconfig> &parser();
public:  static beaconconfig dflt; };

/* Beacon interface.  Every server in the system is expected to send
 * periodic BEACON::resp messages to a broadcast (or in future
 * multicast) interface advertising their capabilities and the address
 * on which peers should connect to them.  Anything which can act as a
 * client should listen for these messages and use them to build up a
 * (agentname,actortype)->peername mapping.  Servers should also
 * listen for BEACON::req messages and respond to them with unicast
 * BEACON::resp messages to whoever sent the req; these are used to
 * populate an initial start-of-day table and for cache maintenance
 * when we're running.
 */
/* Magic numbers on every message because they get sent by UDP
 * broadcast and there's a high risk of seeing messages we aren't
 * expecting. */
namespace proto {
namespace beacon {
class req {
public: req(const clustername &cluster,
            const maybe<agentname> &name,
            maybe<interfacetype> type);
public: req(deserialise1 &);
public: void serialise(serialise1 &) const;
public: static const unsigned magicval = 0x16e3870e;
public: unsigned magic;
public: class ::version version;
public: clustername cluster;
public: maybe<agentname> name;
public: maybe<interfacetype> type;
public: bool operator==(const req &) const; };
class resp {
public: resp(const clustername &,
             const agentname &,
             list<interfacetype>,
             peername::port, timedelta);
public: resp(deserialise1 &);
public: void serialise(serialise1 &) const;
public: static const unsigned magicval = 0x84d206a6;
public: unsigned magic;
public: class ::version version;
public: clustername cluster;
public: agentname name;
public: list<interfacetype> type;
public: peername::port port;
public: timedelta cachetime;
public: bool operator==(const resp &) const; }; } }

#endif /* !BEACON_H__ */
