#include "storageconfig.H"

#include "parsers.H"

#include "parsers.tmpl"

storageconfig::storageconfig(const filename &f, const beaconserverconfig &c)
    : poolpath(f), beacon(c) {}

storageconfig::storageconfig(const quickcheck &q) : poolpath(q), beacon(q) {}

bool
storageconfig::operator==(const storageconfig &o) const {
    return poolpath == o.poolpath && beacon == o.beacon; }

const fields::field &
storageconfig::field() const {
    return
        "<storageconfig:"
        " poolpath:" + poolpath.field() +
        " beacon:" + beacon.field() +
        ">"; }

const parser<storageconfig> &
storageconfig::parser() {
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
