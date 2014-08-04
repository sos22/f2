#include "storageconfig.H"

#include "fields.H"
#include "parsers.H"
#include "test.H"

#include "parsers.tmpl"

storageconfig::storageconfig(const string &_controlsock,
                             const filename &_poolpath,
                             const beaconclientconfig &_beacon,
                             const slavename &_name)
    : controlsock(_controlsock),
      poolpath(_poolpath),
      beacon(_beacon),
      name(_name) {}

storageconfig::storageconfig(const quickcheck &q)
    : controlsock(q.filename()),
      poolpath(q.filename()),
      beacon(q),
      name(q) {}

bool
storageconfig::operator==(const storageconfig &o) const {
    return controlsock == o.controlsock &&
        poolpath == o.poolpath &&
        beacon == o.beacon &&
        name == o.name; }

const fields::field &
fields::mk(const storageconfig &sc) {
    return "<storageconfig: controlsock=" + mk(sc.controlsock).escape() +
        " poolpath=" + mk(sc.poolpath) +
        " beacon=" + mk(sc.beacon) +
        " name=" + mk(sc.name) + ">"; }

const parser<storageconfig> &
parsers::_storageconfig() {
    return ("<storageconfig:" +
            ~(" controlsock=" + strparser) +
            ~(" poolpath=" + _filename()) +
            " beacon=" + _beaconclientconfig() +
            " name=" + _slavename() +
            ">")
        .map<storageconfig>([] (const pair<pair<pair<maybe<const char *>,
                                                     maybe<filename> >,
                                                beaconclientconfig>,
                                           slavename> &x) {
            return storageconfig(
                x.first().first().first().dflt("storageslave"),
                x.first().first().second().dflt(filename("storagepool")),
                x.first().second(),
                x.second()); }); }

void
tests::_storageconfig() {
    testcaseV("storageconfig", "parsers", [] {
            parsers::roundtrip(parsers::_storageconfig()); }); }
