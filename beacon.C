#include "beacon.H"

#include "fields.H"
#include "parsers.H"
#include "serialise.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "pair.tmpl"
#include "parsers.tmpl"
#include "serialise.tmpl"

beaconconfig::beaconconfig(const peernameport &__reqport,
                           const peernameport &__respport)
    : _reqport(__reqport),
      _respport(__respport) {}

beaconconfig::beaconconfig(deserialise1 &ds) : _reqport(ds), _respport(ds) {}

void
beaconconfig::serialise(serialise1 &s) const {
    s.push(reqport());
    s.push(respport()); }

bool
beaconconfig::operator==(const beaconconfig &o) const {
    return reqport() == o.reqport() && respport() == o.respport(); }

const fields::field &
beaconconfig::field() const {
    return
        "<beaconconfig:"
        " reqport:" + reqport().field() +
        " respport:" + respport().field() +
        ">"; }

const parser<beaconconfig> &
beaconconfig::parser() {
    auto &i("<beaconconfig:"
            " reqport:" + peernameport::parser() +
            " respport:" + peernameport::parser() +
            ">");
    class f : public ::parser<beaconconfig> {
    public: decltype(i) inner;
    public: f(decltype(inner) _inner) : inner(_inner) {}
    public: orerror<result> parse(const char *what) const {
        return inner.parse(what).map<result>(
            [] (auto r) {
                return r.map<beaconconfig>([] (auto w) {
                        return beaconconfig(w.first(), w.second()); }); }); } };
    return *new f(i); }

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
