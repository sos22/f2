#include <err.h>

#include "agentname.H"
#include "clustername.H"
#include "computeagent.H"
#include "fields.H"
#include "filename.H"
#include "logging.H"
#include "parsers.H"
#include "pubsub.H"
#include "timedelta.H"

#include "parsers.tmpl"

int
main(int argc, char *argv[]) {
    initlogging("compute");
    initpubsub();
    
    if (argc != 4) {
        errx(1,
             "need three arguments: a cluster name, the filesystem "
             "agent name, and the compute agent name"); }
    auto cluster(parsers::__clustername()
                 .match(argv[1])
                 .fatal("parsing cluster name " + fields::mk(argv[1])));
    auto fsname(parsers::_agentname()
                .match(argv[2])
                .fatal("parsing filesystem name " + fields::mk(argv[2])));
    auto name(parsers::_agentname()
              .match(argv[3])
              .fatal("parsing agent name " + fields::mk(argv[3])));
    
    auto service(computeagent::build(
                     clientio::CLIENTIO,
                     cluster,
                     fsname,
                     name,
                     filename("computestate"))
                 .fatal("listening on computer interface"));
    
    while (true) timedelta::hours(1).future().sleep(clientio::CLIENTIO); }
