#include "storageconfig.H"

#include "fields.H"
#include "parsers.H"
#include "test.H"

#include "parsers.tmpl"

mktupledef(storageconfig)

const parser<storageconfig> &
parsers::__storageconfig() {
    return ("<storageconfig:" +
            ~(" controlsock:" + _peername()) +
            ~(" poolpath:" + _filename()) +
            " beacon:" + __beaconserverconfig() +
            ">")
        .map<storageconfig>(
            []
            (const pair<pair<maybe<peername>,
                             maybe<filename> >,
                        beaconserverconfig> &x) {
            return storageconfig(
                x.first().first().dflt(
                    peername::local(filename("storageslave"))
                    .fatal("peername storageslave")),
                x.first().second().dflt(
                    filename("storagepool")),
                x.second()); }); }

void
tests::__storageconfig() {
    testcaseV("storageconfig", "parsers", [] {
            parsers::roundtrip(parsers::__storageconfig()); }); }
