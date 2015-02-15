#include "storageconfig.H"

#include "fields.H"
#include "parsers.H"
#include "test.H"

#include "parsers.tmpl"

mktupledef(storageconfig)

const parser<storageconfig> &
parsers::__storageconfig() {
    return ("<storageconfig:" +
            ~(" poolpath:" + _filename()) +
            " beacon:" + __beaconserverconfig() +
            ">")
        .map<storageconfig>(
            []
            (const pair<maybe<filename>, beaconserverconfig> &x) {
            return storageconfig(
                x.first().dflt(filename("storagepool")),
                x.second()); }); }

void
tests::__storageconfig() {
    testcaseV("storageconfig", "parsers", [] {
            parsers::roundtrip(parsers::__storageconfig()); }); }
