#include <err.h>

//#include "agentname.H"
#include "clustername.H"
#include "connpool.H"
#include "fields.H"
#include "logging.H"
#include "filesystemagent.H"
#include "parsers.H"
#include "pubsub.H"

#include "parsers.tmpl"

int
main(int argc, char *argv[]) {
    initlogging("filesystem");
    initpubsub();
    
    if (argc != 3) {
        errx(1, "need two arguments, the cluster to join and our own name"); }
    auto cluster(parsers::__clustername()
                 .match(argv[1])
                 .fatal("parsing cluster name " + fields::mk(argv[1])));
    auto name(parsers::_agentname()
              .match(argv[2])
              .fatal("parsing agent name " + fields::mk(argv[2])));
    auto pool(connpool::build(cluster)
              .fatal("creating connection pool"));
    
    filesystemagent(clientio::CLIENTIO,
                    cluster,
                    name,
                    peername::all(peername::port::any))
        .fatal("initialising filesystem agent");
    
    while (true) timedelta::hours(1).future().sleep(clientio::CLIENTIO);
    return 0; }
