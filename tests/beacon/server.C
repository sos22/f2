/* Simple program which sits and advertise an actortype::test server
 * over the beacon protocol with clustername, slavename, and port
 * taken from the command line. */
#include <err.h>

#include "beaconserver.H"
#include "logging.H"
#include "parsers.H"
#include "shutdown.H"

#include "parsers.tmpl"

int
main(int argc, char *argv[]) {
    if (argc != 3) {
        errx(1,
             "need precisely two arguments: "
             "the beacon server config and the control server peername"); }
    initlogging("beaconserver");
    initpubsub();
    auto beaconconfig(parsers::__beaconserverconfig().match(argv[1])
                      .fatal("parsing beacon config"));
    auto controlpeer(parsers::_peername().match(argv[2])
                     .fatal("parsing control peer"));
    waitbox<shutdowncode> w;
    auto c(controlserver::build(controlpeer, w)
           .fatal("starting control server"));
    auto server(beaconserver::build(beaconconfig, c)
                .fatal("starting beacon server"));
    auto r(w.get(clientio::CLIENTIO));
    delete server;
    c->destroy(clientio::CLIENTIO);
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging();
    r.finish(); }
