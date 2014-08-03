#include <err.h>
#include <signal.h>
#include <unistd.h>

#include "beaconserver.H"
#include "controlserver.H"
#include "coordinator.H"
#include "fields.H"
#include "frequency.H"
#include "logging.H"
#include "pubsub.H"
#include "registrationsecret.H"
#include "shutdown.H"
#include "waitbox.H"

int
main()
{
    initlogging("master");
    initpubsub();

    logmsg(loglevel::notice, fields::mk("master starting"));

    signal(SIGPIPE, SIG_IGN);
    waitbox<shutdowncode> s;
    (void)unlink("mastersock");
    auto c(controlserver::build(peername::local("mastersock"), s)
           .fatal("build control interface"));
    logmsg(loglevel::error,
           fields::mk("should use a less guessable registration secret"));
    auto ms(mastersecret::mk());
    auto rs(registrationsecret::mk("<default password>")
            .fatal(fields::mk("cannot build registration secret")));
    auto coord(coordinator::build(ms, rs, peername::tcpany(), c)
               .fatal("build worker coordinator"));

    auto beacon(
        beaconserver::build(beaconserverconfig(rs, coord->localname(), ms), c)
        .fatal("build beacon server"));

    auto r = s.get(clientio::CLIENTIO);

    beacon->destroy(clientio::CLIENTIO);
    coord->destroy(clientio::CLIENTIO);
    c->destroy(clientio::CLIENTIO);
    deinitlogging();
    deinitpubsub(clientio::CLIENTIO);
    r.finish();
}
