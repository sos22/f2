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
            ~(" connconfig:" + _rpcconnconfig()) +
            ">")
        .map<storageconfig>(
            []
            (const pair<pair<pair<maybe<peername>,
                                  maybe<filename> >,
                             beaconserverconfig>,
                        maybe<rpcconnconfig> > &x) {
            return storageconfig(
                x.first().first().first().dflt(
                    peername::local(filename("storageslave"))
                    .fatal("peername storageslave")),
                x.first().first().second().dflt(
                    filename("storagepool")),
                x.first().second(),
                x.second().dflt(rpcconnconfig::dflt)); }); }

void
tests::__storageconfig() {
    testcaseV("storageconfig", "parsers", [] {
            parsers::roundtrip(parsers::__storageconfig()); }); }
