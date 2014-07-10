#include "storageslave.H"

#include "beaconclient.H"
#include "tcpsocket.H"

#include "rpcconn.tmpl"
#include "wireproto.tmpl"

wireproto_simple_wrapper_type(storageslave::status_t,
                              rpcconn::status_t,
                              conn)

const fields::field &
fields::mk(const storageslave::status_t &o) {
    return mk(o.conn); }

void
storageslave::statusiface::getstatus(wireproto::tx_message *msg) const {
    msg->addparam(proto::STATUS::resp::storageslave, owner->status()); }

orerror<storageslave *>
storageslave::build(clientio io,
                    const registrationsecret &rs,
                    controlserver *cs) {
    auto br(beaconclient(rs));
    if (br.isfailure()) return br.failure();
    auto sock(tcpsocket::connect(io, br.success().mastername));
    if (sock.isfailure()) return sock.failure();
    auto sr(rpcconn::connectmaster<storageslave>(io, br.success(), cs));
    if (sr.isfailure()) {
        sock.success().close();
        return sr.failure(); }
    sr.success()->status_.start();
    return sr.success(); }

void
storageslave::destroy(clientio io) {
    status_.stop();
    rpcconn::destroy(io); }

storageslave::status_t
storageslave::status() const {
    return status_t(rpcconn::status(Nothing)); }
