#include <signal.h>

#include "beaconserver.H"
#include "controlserver.H"
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
    if (s.isfailure())
        s.failure().fatal("build shutdown box");
    auto c(controlserver::build("mastersock", s.success()));
    if (c.isfailure())
        c.failure().fatal("build control interface");
    auto beacon(beaconserver::build(
                    registrationsecret::mk("<default password>"),
                    frequency::hz(10),
                    c.success()));
    if (beacon.isfailure())
        beacon.failure().fatal("build beacon server");
    auto r = s.success()->get();
    beacon.success()->destroy();
    c.success().destroy();
    s.success()->destroy();
    deinitlogging();
    r.finish();
}
