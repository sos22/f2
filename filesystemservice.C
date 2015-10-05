#include <err.h>

#include "clustername.H"
#include "connpool.H"
#include "fields.H"
#include "filesystemagent.H"
#include "logging.H"
#include "main.H"
#include "parsers.H"
#include "pubsub.H"

#include "parsers.tmpl"

orerror<void>
f2main(list<string> &args) {
    initlogging("filesystem");
    initpubsub();
    
    if (args.length() != 2) {
        errx(1, "need two arguments, the cluster to join and our own name"); }
    auto cluster(parsers::__clustername()
                 .match(args.idx(0))
                 .fatal("parsing cluster name " + fields::mk(args.idx(0))));
    auto name(parsers::_agentname()
              .match(args.idx(1))
              .fatal("parsing agent name " + fields::mk(args.idx(1))));
    auto pool(connpool::build(cluster)
              .fatal("creating connection pool"));
    
    filesystemagent(clientio::CLIENTIO,
                    cluster,
                    name,
                    peername::all(peername::port::any))
        .fatal("initialising filesystem agent");
    
    while (true) timedelta::hours(1).future().sleep(clientio::CLIENTIO); }
