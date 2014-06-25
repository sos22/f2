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
    initpubsub();

    logmsg(loglevel::notice, fields::mk("storage slave starting"));

    signal(SIGPIPE, SIG_IGN);
    waitbox<shutdowncode> s;
    (void)unlink("storageslave");
    auto c(controlserver::build(
               peername::local("storageslave"), s));
    if (c.isfailure()) c.failure().fatal("build control interface");
    auto slave(storageslave::build(
                   clientio::CLIENTIO,
                   registrationsecret::mk("<default password>"),
                   c.success()));
    if (slave.isfailure())
        slave.failure().fatal("build storage slave");

    auto r = s.get();
    slave.success()->destroy();
    c.success()->destroy(clientio::CLIENTIO);
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging();
    r.finish();
}
