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
    auto c(controlserver::build(peername::local("mastersock"), s));
    if (c.isfailure()) c.failure().fatal("build control interface");
    
    logmsg(loglevel::error,
           fields::mk("should use less guessable registration and master secrets"));
    mastersecret ms("<master secret>");
    auto rs(registrationsecret::mk("<default password>"));
    
    auto coord(coordinator::build(
                   ms,
                   rs,
                   peername::tcpany(),
                   c.success()));
    if (coord.isfailure()) coord.failure().fatal("build worker coordinator");
    
    auto beacon(beaconserver::build(
                    beaconserverconfig(rs, coord.success()->localname(), ms),
                    c.success()));
    if (beacon.isfailure()) beacon.failure().fatal("build beacon server");

    auto r = s.get();

    beacon.success()->destroy(clientio::CLIENTIO);
    coord.success()->destroy(clientio::CLIENTIO);
    c.success()->destroy(clientio::CLIENTIO);
    deinitlogging();
    deinitpubsub(clientio::CLIENTIO);
    fields::flush();
    r.finish();
}
