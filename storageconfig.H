#ifndef STORAGECONFIG_H__
#define STORAGECONFIG_H__

#include "beaconserver.H"
#include "filename.H"
#include "rpcconn.H"
#include "slavename.H"
#include "tuple.H"

#define _storageconfig(ctxt, iter0, iter1)                    \
    iter0(ctxt, 1, mand, ref, peername, controlsock)          \
    iter0(ctxt, 2, mand, ref, filename, poolpath)             \
    iter0(ctxt, 3, mand, ref, beaconserverconfig, beacon)     \
    iter1(ctxt, 4, mand, ref, rpcconnconfig, connconfig)
class storageconfig {
    mktuplefields(storageconfig, public); };
mktupleext(storageconfig);

template <typename> class parser;
namespace parsers {
const parser<storageconfig> &__storageconfig();
}

namespace tests {
void __storageconfig(); }

#endif /* !STORAGECONFIG_H__ */
