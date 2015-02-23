#include <err.h>
#include <signal.h>
#include <unistd.h>

#include "fields.H"
#include "filename.H"
#include "logging.H"
#include "nnp.H"
#include "parsers.H"
#include "peername.H"
#include "pubsub.H"
#include "shutdown.H"
#include "storageconfig.H"
#include "storageagent.H"
#include "string.H"
#include "waitbox.H"

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

    logmsg(loglevel::notice, fields::mk("storage agent starting"));

    signal(SIGPIPE, SIG_IGN);

    storageagent::build(clientio::CLIENTIO, config)
        .fatal("build storage agent");

    while (true) sleep(1000); }
