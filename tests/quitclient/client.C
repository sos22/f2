#include <err.h>

#include "fields.H"
#include "logging.H"
#include "proto.H"
#include "rpcconn.H"
#include "shutdown.H"

#include "rpcconn.tmpl"

int
main(int argc, char *argv[]) {
    if (argc != 2) errx(1, "need a single argument, the peername to quit");
    initlogging("quitclient");
    initpubsub();
    auto peer(peername::parse(argv[1]));
    if (peer.isfailure()) {
        peer.failure().fatal("parsing " + fields::mk(argv[1])); }
    auto conn(rpcconn::connect<rpcconn>(clientio::CLIENTIO, peer.success()));
    if (conn.isfailure()) {
        conn.failure().fatal("connecting to " + fields::mk(peer.success())); }
    auto cr(conn.success()->send(
                clientio::CLIENTIO,
                wireproto::tx_message(proto::QUIT::tag)
                .addparam(proto::QUIT::req::message, "quit now")
                .addparam(proto::QUIT::req::reason, shutdowncode::ok)));
    if (cr.isjust()) cr.just().fatal("sending QUIT");
    conn.success()->drain(clientio::CLIENTIO);
    conn.success()->destroy(clientio::CLIENTIO);
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging();
    return 0; }
