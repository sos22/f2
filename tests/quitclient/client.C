#include <err.h>

#include "fields.H"
#include "logging.H"
#include "parsers.H"
#include "proto.H"
#include "rpcconn.H"
#include "shutdown.H"

#include "rpcconn.tmpl"

int
main(int argc, char *argv[]) {
    if (argc != 2) errx(1, "need a single argument, the peername to quit");
    initlogging("quitclient");
    initpubsub();
    auto peer(parsers::_peername()
              .match(argv[1])
              .fatal("parsing " + fields::mk(argv[1])));
    auto conn(rpcconn::connect<rpcconn>(
                  clientio::CLIENTIO,
                  rpcconnauth::mkdone(
                      slavename("<quit server>"),
                      actortype::cli,
                      rpcconnconfig::dflt,
                      NULL),
                  peer,
                  rpcconnconfig::dflt)
              .fatal("connecting to " + fields::mk(peer)));
    delete conn->call(
        clientio::CLIENTIO,
        wireproto::req_message(proto::QUIT::tag, conn->allocsequencenr())
        .addparam(proto::QUIT::req::message, "quit now")
        .addparam(proto::QUIT::req::reason, shutdowncode::ok))
        .fatal("sending QUIT");
    conn->destroy(clientio::CLIENTIO);
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging();
    return 0; }
