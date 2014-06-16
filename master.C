#include <signal.h>
#include <unistd.h>

#include "beaconserver.H"
#include "controlserver.H"
#include "coordinator.H"
#include "fields.H"
#include "frequency.H"
#include "logging.H"
#include "registrationsecret.H"
#include "shutdown.H"
#include "waitbox.H"

int
main()
{
    initlogging("master");

    logmsg(loglevel::notice, fields::mk("master starting"));

    signal(SIGPIPE, SIG_IGN);
    auto s(waitbox<shutdowncode>::build());
    if (s.isfailure()) s.failure().fatal("build shutdown box");
    (void)unlink("mastersock");
    auto c(controlserver::build(peername::local("mastersock"), s.success()));
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
                    rs,
                    frequency::hz(10),
                    c.success(),
                    coord.success()->localname(),
                    ms));
    if (beacon.isfailure()) beacon.failure().fatal("build beacon server");

    auto r = s.success()->get();

    beacon.success()->destroy();
    coord.success()->destroy();
    c.success()->destroy();
    delete s.success();
    deinitlogging();
    fields::flush();
    r.finish();
}
