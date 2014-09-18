/* Simple program which joins a cluster as a client and then prints
 * out whever a new server arrives. */
#include <err.h>

#include "beaconclient.H"
#include "logging.H"
#include "parsers.H"
#include "shutdown.H"

#include "parsers.tmpl"

int
main(int argc, char *argv[]) {
    if (argc != 3) {
        errx(1,
             "need precisely two arguments: "
             "the beacon client config and the control server peername"); }
    initlogging("beaconclient");
    initpubsub();
    auto beaconconfig(parsers::__beaconclientconfig().match(argv[1])
                      .fatal("parsing beacon config"));
    auto controlpeer(parsers::_peername().match(argv[2])
                     .fatal("parsing control peer"));
    waitbox<shutdowncode> w;
    auto c(controlserver::build(controlpeer, w)
           .fatal("starting control server"));
    auto client(beaconclient::build(beaconconfig, c)
                .fatal("starting beacon client"));
    subscriber sub;
    subscription shutdownsub(sub, w.pub);
    subscription clientsub(sub, client->changed);

    while (!w.ready()) {
        logmsg(loglevel::info, "Current cache contents:");
        for (auto it(client->start()); !it.finished(); it.next()) {
            logmsg(loglevel::info,
                   "name: " + fields::mk(it.name()) +
                   "type: " + fields::mk(it.type()) +
                   "peer: " + fields::mk(it.peer())); }
        sub.wait(clientio::CLIENTIO); }
    client->destroy(clientio::CLIENTIO);
    c->destroy(clientio::CLIENTIO);
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging();
    w.poll().just().finish(); }
