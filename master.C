#include <signal.h>
#include <unistd.h>

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
    if (s.isfailure()) s.failure().fatal("build shutdown box");
    (void)unlink("mastersock");
    auto c(controlserver::build(peername::local("mastersock"), s.success()));
    if (c.isfailure()) c.failure().fatal("build control interface");
    logmsg(loglevel::error,
           fields::mk("should use less guessable registration and master secrets"));
    auto beacon(beaconserver::build(
                    registrationsecret::mk("<default password>"),
                    frequency::hz(10),
                    c.success(),
                    peername::local("dummy"),
                    mastersecret("<master secret>")));
    if (beacon.isfailure()) beacon.failure().fatal("build beacon server");
    auto r = s.success()->get();
    beacon.success()->destroy();
    c.success()->destroy();
    delete s.success();
    deinitlogging();
    r.finish();
}
