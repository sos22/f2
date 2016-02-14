#include <err.h>
#include <signal.h>
#include <unistd.h>

#include "fields.H"
#include "filename.H"
#include "logging.H"
#include "main.H"
#include "nnp.H"
#include "parsers.H"
#include "peername.H"
#include "pubsub.H"
#include "shutdown.H"
#include "storageconfig.H"
#include "storageagent.H"
#include "string.H"
#include "waitbox.H"

#include "parsers.tmpl"

orerror<void>
f2main(list<string> &args)
{
    if (args.length() != 1) {
        errx(1, "need one argument, the storage configuration"); }

    auto config(storageconfig::parser()
                .match(args.idx(0))
                .fatal("cannot parse " + fields::mk(args.idx(0)) +
                       " as storage configuration"));

    initpubsub();
    logmsg(loglevel::notice, fields::mk("storage agent starting"));

    signal(SIGPIPE, SIG_IGN);

    {   auto e(storageagent::build(clientio::CLIENTIO, config));
        if (e.isfailure()) {
            deinitpubsub(clientio::CLIENTIO);
            e.fatal("build storage agent"); } }

    while (true) sleep(1000); }
