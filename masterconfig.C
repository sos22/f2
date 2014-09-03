#include "masterconfig.H"

#include "fields.H"
#include "parsers.H"
#include "test.H"

#include "parsers.tmpl"

masterconfig::masterconfig(const peername &__controlsock,
                           const registrationsecret &__rs,
                           const peername &__listenon,
                           const peername::port &__beaconport,
                           const ratelimiterconfig &__beaconlimit,
                           const rpcconnconfig &__connconfig)
    : controlsock(__controlsock),
      rs(__rs),
      listenon(__listenon),
      beaconport(__beaconport),
      beaconlimit(__beaconlimit),
      connconfig(__connconfig) {}

masterconfig::masterconfig(const registrationsecret &__rs)
    : controlsock(peername::local(filename("mastersock"))
                  .fatal("peername mastersock")),
      rs(__rs),
      listenon(peername::all(peername::port::any)),
      beaconport(9009),
      beaconlimit(ratelimiterconfig(frequency::hz(10), 100)),
      connconfig(rpcconnconfig::dflt) {}

masterconfig::masterconfig(const quickcheck &q)
    : controlsock(q),
      rs(q),
      listenon(q),
      beaconport(q),
      beaconlimit(q),
      connconfig(q) {}

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
mksetter(rpcconnconfig, connconfig)
#undef mksetter

bool
masterconfig::operator==(const masterconfig &o) const {
    return controlsock == o.controlsock &&
        rs == o.rs &&
        listenon == o.listenon &&
        beaconport == o.beaconport &&
        beaconlimit == o.beaconlimit &&
        connconfig == o.connconfig; }

const parser<masterconfig> &
parsers::_masterconfig() {
    return ("<masterconfig:" +
            ~(" controlsock:" + _peername()) +
            (" rs:" + _registrationsecret()) +
            ~(" listenon:" + _peername()) +
            ~(" beaconport:" + _peernameport()) +
            ~(" beaconlimit:" + _ratelimiterconfig()) +
            ~(" connconfig:" + _rpcconnconfig()) +
            ">")
        .map<masterconfig>(
            [] (const pair<pair<pair<pair<pair<maybe<peername>,
                                               registrationsecret>,
                                          maybe<peername> >,
                                     maybe<peername::port> >,
                                maybe<ratelimiterconfig> >,
                           maybe<rpcconnconfig> > &x) {
                return masterconfig(
                    x.first().first().first().first().first().dflt(
                        peername::local(filename("mastersock"))
                        .fatal("peername mastersock")),
                    x.first().first().first().first().second(),
                    x.first().first().first().second().dflt(
                        peername::all(peername::port::any)),
                    x.first().first().second().dflt(peername::port(9009)),
                    x.first().second().dflt(
                        ratelimiterconfig(frequency::hz(10), 100)),
                    x.second().dflt(rpcconnconfig::dflt)); }); }

const fields::field &
fields::mk(const masterconfig &config) {
    return "<masterconfig: controlsock:" + fields::mk(config.controlsock) +
        " rs:" + fields::mk(config.rs) +
        " listenon:" + fields::mk(config.listenon) +
        " beaconport:" + fields::mk(config.beaconport) +
        " beaconlimit:" + fields::mk(config.beaconlimit) +
        " connconfig:" + fields::mk(config.connconfig) +
        ">"; }

void
tests::_masterconfig() {
    testcaseV("masterconfig", "parser", [] {
            parsers::roundtrip(parsers::_masterconfig()); });
    testcaseV("masterconfig", "parser2", [] {
            assert(parsers::_masterconfig()
                   .match("<masterconfig: rs:<registrationsecret:password>>") ==
                   masterconfig(registrationsecret::mk("password")
                                .fatal(fields::mk("make rs")))); });
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
                   ratelimiterconfig(frequency::hz(2.5), 12));
            rpcconnconfig cc((quickcheck()));
            assert(ms._connconfig(cc).connconfig == cc); }); }
