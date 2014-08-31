#include "masterconfig.H"

#include "fields.H"
#include "parsers.H"
#include "test.H"

#include "parsers.tmpl"

masterconfig::masterconfig(const peername &__controlsock,
                           const registrationsecret &__rs,
                           const peername &__listenon,
                           const peername::port &__beaconport,
                           const ratelimiterconfig &__beaconlimit)
    : controlsock(__controlsock),
      rs(__rs),
      listenon(__listenon),
      beaconport(__beaconport),
      beaconlimit(__beaconlimit) {}

masterconfig::masterconfig(const registrationsecret &__rs)
    : controlsock(peername::local(filename("mastersock"))
                  .fatal("peername mastersock")),
      rs(__rs),
      listenon(peername::all(peername::port::any)),
      beaconport(9009),
      beaconlimit(ratelimiterconfig(frequency::hz(10), 100)) {}

masterconfig::masterconfig(const quickcheck &q)
    : controlsock(q),
      rs(q),
      listenon(q),
      beaconport(q),
      beaconlimit(q) {}

#define mksetter(type, name)                            \
    masterconfig                                        \
    masterconfig::_ ## name(const type &s) const {      \
        masterconfig res(*this);                        \
        res.name = s;                                   \
        return res; }
mksetter(peername, controlsock)
mksetter(registrationsecret, rs)
mksetter(peername, listenon)
mksetter(peername::port, beaconport)
mksetter(ratelimiterconfig, beaconlimit)
#undef mksetter

bool
masterconfig::operator==(const masterconfig &o) const {
    return controlsock == o.controlsock &&
        rs == o.rs &&
        listenon == o.listenon &&
        beaconport == o.beaconport &&
        beaconlimit == o.beaconlimit; }

const parser<masterconfig> &
parsers::_masterconfig() {
    return ("<masterconfig:" +
            ~(" controlsock:" + _peername()) +
            (" rs:" + _registrationsecret()) +
            ~(" listenon:" + _peername()) +
            ~(" beaconport:" + _peernameport()) +
            ~(" beaconlimit:" + _ratelimiterconfig()) +
            ">")
        .map<masterconfig>(
            [] (const pair<pair<pair<pair<maybe<peername>,
                                          registrationsecret>,
                                     maybe<peername> >,
                                maybe<peername::port> >,
                           maybe<ratelimiterconfig> > &x) {
                return masterconfig(
                    x.first().first().first().first().dflt(
                        peername::local(filename("mastersock"))
                        .fatal("peername mastersock")),
                    x.first().first().first().second(),
                    x.first().first().second().dflt(peername::all(peername::port::any)),
                    x.first().second().dflt(peername::port(9009)),
                    x.second().dflt(
                        ratelimiterconfig(frequency::hz(10), 100))); }); }

const fields::field &
fields::mk(const masterconfig &config) {
    return "<masterconfig: controlsock:" + fields::mk(config.controlsock) +
        " rs:" + fields::mk(config.rs) +
        " listenon:" + fields::mk(config.listenon) +
        " beaconport:" + fields::mk(config.beaconport) +
        " beaconlimit:" + fields::mk(config.beaconlimit) + ">"; }

void
tests::_masterconfig() {
    testcaseV("masterconfig", "parser", [] {
            parsers::roundtrip(parsers::_masterconfig()); });
    testcaseV("masterconfig", "fields", [] {
            masterconfig ms(registrationsecret::mk("foo")
                            .fatal(fields::mk("foo")));
            assert(ms._controlsock(peername::local(filename("x"))
                                   .fatal("local peername x"))
                   .controlsock ==
                   peername::local(filename("x"))
                   .fatal("local peername x"));
            auto r(registrationsecret::mk("bar")
                   .fatal(fields::mk("bar")));
            assert(ms._rs(r).rs == r);
            assert(ms._listenon(peername::local(filename("hello"))
                                .fatal("local peername hello")).listenon ==
                   peername::local(filename("hello"))
                   .fatal("local peername hello"));
            assert(ms._beaconport(peername::port(99)).beaconport ==
                   peername::port(99));
            assert(ms._beaconlimit(
                       ratelimiterconfig(frequency::hz(2.5), 12)).beaconlimit ==
                   ratelimiterconfig(frequency::hz(2.5), 12)); }); }
