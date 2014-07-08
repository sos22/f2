#include <err.h>
#include <signal.h>
#include <unistd.h>

#include "controlserver.H"
#include "fields.H"
#include "logging.H"
#include "peername.H"
#include "pubsub.H"
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
    auto rs(registrationsecret::mk("<default password>"));
    if (rs == Nothing) errx(1, "cannot construct registration secret");
    auto slave(storageslave::build(
                   clientio::CLIENTIO,
                   rs.just(),
                   c.success()));
    if (slave.isfailure())
        slave.failure().fatal("build storage slave");

    auto r = s.get();
    slave.success()->destroy(clientio::CLIENTIO);
    c.success()->destroy(clientio::CLIENTIO);
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging();
    r.finish();
}
