#include "storageslave.H"

#include "beaconclient.H"
#include "tcpsocket.H"

#include "rpcconn.tmpl"

orerror<storageslave *>
storageslave::build(clientio io,
                    const registrationsecret &rs,
                    controlserver *) {
    auto br(beaconclient(rs));
    if (br.isfailure()) return br.failure();
    auto sock(tcpsocket::connect(io, br.success().mastername));
    if (sock.isfailure()) return sock.failure();
    auto sr(rpcconn::connectmaster<storageslave>(io, br.success()));
    if (sr.isfailure()) {
        sock.success().close();
        return sr.failure(); }
    return sr.success(); }
