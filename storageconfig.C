#include "storageconfig.H"

#include "parsers.H"

#include "parsers.tmpl"

mktupledef(storageconfig)

const parser<storageconfig> &
parsers::__storageconfig() {
    return ("<storageconfig:" +
            ~(" poolpath:" + filename::parser()) +
            " beacon:" + beaconserverconfig::parser() +
            ">")
        .map<storageconfig>(
            []
            (const pair<maybe<filename>, beaconserverconfig> &x) {
            return storageconfig(
                x.first().dflt(filename("storagepool")),
                x.second()); }); }
