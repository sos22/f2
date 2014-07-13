#include <err.h>

#include "fields.H"
#include "logging.H"
#include "proto.H"
#include "rpcconn.H"

#include "rpcconn.tmpl"

int
main(int argc, char *argv[]) {
    if (argc != 2) errx(1, "need a single argument, the peername to ping");
    initlogging("pingclient");
    initpubsub();
    auto peer(peername::parse(argv[1]));
    if (peer.isfailure()) {
        peer.failure().fatal("parsing " + fields::mk(argv[1])); }
    auto conn(rpcconn::connect<rpcconn>(
                  clientio::CLIENTIO,
                  rpcconnauth::mkdone(),
                  peer.success()));
    if (conn.isfailure()) {
        conn.failure().fatal("connecting to " + fields::mk(peer.success())); }
    auto snr(conn.success()->allocsequencenr());
    auto cr(conn.success()->call(
                clientio::CLIENTIO,
                wireproto::req_message(proto::PING::tag, snr)));
    conn.success()->putsequencenr(snr);
    if (cr.isfailure()) {
        cr.failure().fatal("sending PING"); }
    fields::print(
        "PING " + fields::mk(cr.success()->getparam(
                                 proto::PING::resp::cntr)));
    delete cr.success();
    conn.success()->destroy(clientio::CLIENTIO);
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging();
    return 0; }
