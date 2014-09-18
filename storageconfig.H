#ifndef STORAGECONFIG_H__
#define STORAGECONFIG_H__

#include "beaconserver.H"
#include "filename.H"
#include "rpcconn.H"
#include "slavename.H"
#include "tuple.H"

#define _storageconfig(ctxt, iter0, iter1)              \
    iter0(ctxt, 1, ref, peername, controlsock)          \
    iter0(ctxt, 2, ref, filename, poolpath)             \
    iter0(ctxt, 3, ref, beaconserverconfig, beacon)     \
    iter1(ctxt, 4, ref, rpcconnconfig, connconfig)
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
