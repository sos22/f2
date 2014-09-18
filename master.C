#include <err.h>
#include <signal.h>
#include <unistd.h>

#include "beaconserver.H"
#include "controlserver.H"
#include "fields.H"
#include "frequency.H"
#include "logging.H"
#include "masterconfig.H"
#include "parsers.H"
#include "pubsub.H"
#include "shutdown.H"
#include "waitbox.H"

#include "orerror.tmpl"

int
main(int argc, char *argv[])
{
    initlogging("master");
    initpubsub();

    if (argc != 2) errx(1, "need a masterconfig as single argument");
    auto config(parsers::_masterconfig()
                .match(argv[1])
                .fatal("cannot parse master config " + fields::mk(argv[1])));

    logmsg(loglevel::notice, fields::mk("master starting"));

    signal(SIGPIPE, SIG_IGN);
    waitbox<shutdowncode> s;
    config.controlsock.evict();
    auto c(controlserver::build(config.controlsock, s)
           .fatal("build control interface"));
    auto ms(mastersecret::mk());
    auto coord(coordinator::build(ms,
                                  config.rs,
                                  config.listenon,
                                  c,
                                  config.connconfig)
               .fatal("build worker coordinator"));

    auto beacon(
        beaconserver::build(beaconserverconfig(
                                config.rs,
                                coord->localname(),
                                config.beaconlimit,
                                ms,
                                config.beaconport), c)
        .fatal("build beacon server"));

    auto r = s.get(clientio::CLIENTIO);

    beacon->destroy(clientio::CLIENTIO);
    coord->destroy(clientio::CLIENTIO);
    c->destroy(clientio::CLIENTIO);
    deinitlogging();
    deinitpubsub(clientio::CLIENTIO);
    r.finish();
}
