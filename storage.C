#include <signal.h>
#include <unistd.h>

#include "fields.H"
#include "logging.H"
#include "peername.H"
#include "registrationsecret.H"
#include "shutdown.H"
#include "storageslave.H"
#include "waitbox.H"

int
main()
{
    initlogging("storage");

    logmsg(loglevel::notice, fields::mk("storage slave starting"));

    signal(SIGPIPE, SIG_IGN);
    auto s(waitbox<shutdowncode>::build());
    if (s.isfailure()) s.failure().fatal("build shutdown box");
    (void)unlink("storageslave");
    auto c(controlserver::build(
               peername::local("storageslave"), s.success()));
    if (c.isfailure()) c.failure().fatal("build control interface");
    auto slave(storageslave::build(
                   registrationsecret::mk("<default password>"),
                   c.success()));
    if (slave.isfailure())
        slave.failure().fatal("build storage slave");

    auto r = s.success()->get();
    slave.success()->destroy();
    c.success()->destroy();
    delete s.success();
    deinitlogging();
    r.finish();
}