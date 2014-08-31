#include <err.h>

#include "fields.H"
#include "logging.H"
#include "parsers.H"
#include "proto.H"
#include "rpcconn.H"

#include "rpcconn.tmpl"

int
main(int argc, char *argv[]) {
    if (argc != 2) errx(1, "need a single argument, the peername to ping");
    initlogging("pingclient");
    initpubsub();
    auto peer(parsers::_peername()
              .match(argv[1])
              .fatal("parsing " + fields::mk(argv[1])));
    auto conn(rpcconn::connect<rpcconn>(
                  clientio::CLIENTIO,
                  rpcconnauth::mkdone(rpcconnconfig::dflt),
                  peer,
                  rpcconnconfig::dflt)
              .fatal("connecting to " + fields::mk(peer)));
    auto cr(conn->call(
                clientio::CLIENTIO,
                wireproto::req_message(proto::PING::tag,
                                       conn->allocsequencenr()))
            .fatal("sending PING"));
    fields::print(
        "PING " + fields::mk(cr->getparam(proto::PING::resp::cntr)));
    delete cr;
    conn->destroy(clientio::CLIENTIO);
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging();
    return 0; }
