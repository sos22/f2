#ifndef BEACON_H__
#define BEACON_H__

#include "peername.H"
#include "tuple.H"

/* Fields which a user might want to change which relate to the beacon
 * protocol. */
#define _beaconconfig(ctxt, iter0, iter1)                               \
    /* What UDP port do we use for requests? */                         \
    iter0(ctxt, 1, value, peernameport, reqport)                        \
    /* What UDP port do we use for responses? */                        \
    iter1(ctxt, 2, value, peernameport, respport)
class beaconconfig {
    mktuplefields(beaconconfig, public);
public: static beaconconfig dflt; };
mktupleext(beaconconfig);

#endif /* !BEACON_H__ */
