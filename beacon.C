#include "beacon.H"

#include "fields.H"
#include "parsers.H"
#include "serialise.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "pair.tmpl"
#include "parsers.tmpl"
#include "serialise.tmpl"

beaconconfig::beaconconfig(const peernameport &_reqport,
                           const peernameport &_respport)
    : reqport(_reqport),
      respport(_respport) {}

beaconconfig::beaconconfig(const quickcheck &q) : reqport(q), respport(q) {}

bool
beaconconfig::operator==(const beaconconfig &o) const {
    return reqport == o.reqport && respport == o.respport; }

const fields::field &
beaconconfig::field() const {
    return
        "<beaconconfig:"
        " reqport:" + reqport.field() +
        " respport:" + respport.field() +
        ">"; }

const parser<beaconconfig> &
beaconconfig::parser() {
    return ("<beaconconfig:"
            " reqport:" + parsers::_peernameport() +
            " respport:" + parsers::_peernameport() +
            ">")
        .map<beaconconfig>([] (pair<peernameport, peernameport> w) {
                return beaconconfig(w.first(), w.second()); }); }

beaconconfig
beaconconfig::dflt(
    /* reqport */
    peername::port(9003),
    /* respport */
    peername::port(9004));

proto::beacon::req::req(const clustername &_cluster,
                        const maybe<agentname> &_name,
                        maybe<interfacetype> _type)
    : magic(magicval),
      version(version::current),
      cluster(_cluster),
      name(_name),
      type(_type) {}

proto::beacon::req::req(deserialise1 &ds)
    : magic(ds.poprange(magicval, magicval)),
      version(ds),
      cluster(ds),
      name(ds),
      type(ds) { }

void
proto::beacon::req::serialise(serialise1 &s) const {
    s.push(magic);
    version.serialise(s);
    cluster.serialise(s);
    name.serialise(s);
    type.serialise(s); }

bool
proto::beacon::req::operator==(const req &o) const {
    return magic == o.magic &&
        version == o.version &&
        cluster == o.cluster &&
        name == o.name &&
        type == o.type; }

proto::beacon::resp::resp(const clustername &_cluster,
                          const agentname &_name,
                          list<interfacetype> _type,
                          peername::port _port,
                          timedelta _cachetime)
    : magic(magicval),
      version(::version::current),
      cluster(_cluster),
      name(_name),
      type(_type),
      port(_port),
      cachetime(_cachetime) {
    sort<interfacetype>(
        type,
        [] (const interfacetype &a, const interfacetype &b) {
            return a.ord(b); }); }

proto::beacon::resp::resp(deserialise1 &ds)
    : magic(ds.poprange(magicval, magicval)),
      version(ds),
      cluster(ds),
      name(ds),
      type(ds),
      port(ds),
      cachetime(ds) {
    sort<interfacetype>(
        type,
        [] (const interfacetype &a, const interfacetype &b) {
            return a.ord(b); }); }

void
proto::beacon::resp::serialise(serialise1 &s) const {
    s.push(magic);
    version.serialise(s);
    cluster.serialise(s);
    name.serialise(s);
    type.serialise(s);
    port.serialise(s);
    cachetime.serialise(s); }

bool
proto::beacon::resp::operator==(const resp &o) const {
    return magic == o.magic &&
        version == o.version &&
        cluster == o.cluster &&
        name == o.name &&
        type == o.type &&
        port == o.port &&
        cachetime == o.cachetime; }
