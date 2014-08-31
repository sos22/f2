#include "storageconfig.H"

#include "fields.H"
#include "parsers.H"
#include "test.H"

#include "parsers.tmpl"

storageconfig::storageconfig(const peername &_controlsock,
                             const filename &_poolpath,
                             const beaconclientconfig &_beacon,
                             const slavename &_name,
                             const peername &_listenon,
                             const rpcconnconfig &_connconfig)
    : controlsock(_controlsock),
      poolpath(_poolpath),
      beacon(_beacon),
      name(_name),
      listenon(_listenon),
      connconfig(_connconfig) {}

storageconfig::storageconfig(const quickcheck &q)
    : controlsock(q),
      poolpath(q.filename()),
      beacon(q),
      name(q),
      listenon(q),
      connconfig(q) {}

bool
storageconfig::operator==(const storageconfig &o) const {
    return controlsock == o.controlsock &&
        poolpath == o.poolpath &&
        beacon == o.beacon &&
        name == o.name &&
        listenon == o.listenon &&
        connconfig == o.connconfig; }

const fields::field &
fields::mk(const storageconfig &sc) {
    return "<storageconfig: controlsock=" + mk(sc.controlsock) +
        " poolpath=" + mk(sc.poolpath) +
        " beacon=" + mk(sc.beacon) +
        " name=" + mk(sc.name) +
        " listenon=" + mk(sc.listenon) +
        " connconfig=" + mk(sc.connconfig) +
        ">"; }

const parser<storageconfig> &
parsers::_storageconfig() {
    return ("<storageconfig:" +
            ~(" controlsock=" + _peername()) +
            ~(" poolpath=" + _filename()) +
            " beacon=" + _beaconclientconfig() +
            " name=" + _slavename() +
            ~(" listenon=" + _peername()) +
            ~(" connconfig=" + _rpcconnconfig()) +
            ">")
        .map<storageconfig>([] (const pair<pair<pair<pair<pair<maybe<peername>,
                                                               maybe<filename> >,
                                                          beaconclientconfig>,
                                                     slavename>,
                                                maybe<peername> >,
                                           maybe<rpcconnconfig> > &x) {
            return storageconfig(
                x.first().first().first().first().first().dflt(
                    peername::local(filename("storageslave"))
                    .fatal("peername storageslave")),
                x.first().first().first().first().second().dflt(
                    filename("storagepool")),
                x.first().first().first().second(),
                x.first().first().second(),
                x.first().second().dflt(
                    peername::all(peername::port::any)),
                x.second().dflt(rpcconnconfig::dflt)); }); }

void
tests::_storageconfig() {
    testcaseV("storageconfig", "parsers", [] {
            parsers::roundtrip(parsers::_storageconfig()); }); }
