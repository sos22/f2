#include <err.h>
#include <signal.h>
#include <unistd.h>

#include "controlserver.H"
#include "fields.H"
#include "filename.H"
#include "logging.H"
#include "parsers.H"
#include "peername.H"
#include "pubsub.H"
#include "shutdown.H"
#include "storageconfig.H"
#include "storageslave.H"
#include "string.H"
#include "waitbox.H"

#include "orerror.tmpl"

int
main(int argc, char *argv[])
{
    initlogging("storage");
    initpubsub();

    if (argc != 2) errx(1, "need one argument, the storage configuration");

    auto config(parsers::__storageconfig()
                .match(argv[1])
                .fatal("cannot parse " + fields::mk(argv[1]) +
                       " as storage configuration"));

    logmsg(loglevel::notice, fields::mk("storage slave starting"));

    signal(SIGPIPE, SIG_IGN);

    waitbox<shutdowncode> s;
    config.controlsock.evict();
    auto c(controlserver::build(config.controlsock, s)
           .fatal("build control interface"));
    auto slave(storageslave::build(config, c)
               .fatal("build storage slave"));

    auto r = s.get(clientio::CLIENTIO);
    slave->destroy(clientio::CLIENTIO);
    c->destroy(clientio::CLIENTIO);
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging();
    r.finish();
}
