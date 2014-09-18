#include "beacon.H"

#include "parsers.H"

#include "parsers.tmpl"

mktupledef(beaconconfig);

const parser<beaconconfig> &
parsers::__beaconconfig() {
    return ("<beaconconfig:"
            " reqport:" + parsers::_peernameport() +
            " respport:" + parsers::_peernameport() +
            ">")
        .map<beaconconfig>([] (pair<peernameport, peernameport> w) {
                return beaconconfig(w.first(), w.second()); }); }

beaconconfig
beaconconfig::dflt(
    /* reqport */
    peername::port(9003),
    /* respport */
    peername::port(9004));
