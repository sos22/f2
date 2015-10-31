#include <err.h>

#include "agentname.H"
#include "clustername.H"
#include "computeagent.H"
#include "fields.H"
#include "filename.H"
#include "logging.H"
#include "main.H"
#include "parsers.H"
#include "pubsub.H"
#include "timedelta.H"

#include "parsers.tmpl"

orerror<void>
f2main(list<string> &args) {
    initpubsub();
    
    if (args.length() != 3) {
        errx(1,
             "need three arguments: a cluster name, the filesystem "
             "agent name, and the compute agent name"); }
    auto cluster(parsers::__clustername()
                 .match(args.idx(0))
                 .fatal("parsing cluster name " + fields::mk(args.idx(0))));
    auto fsname(agentname::parser()
                .match(args.idx(1))
                .fatal("parsing filesystem name " + fields::mk(args.idx(1))));
    auto name(agentname::parser()
              .match(args.idx(2))
              .fatal("parsing agent name " + fields::mk(args.idx(2))));
    
    auto service(computeagent::build(
                     clientio::CLIENTIO,
                     cluster,
                     fsname,
                     name,
                     filename("computestate"))
                 .fatal("listening on compute interface"));
    
    while (true) timedelta::hours(1).future().sleep(clientio::CLIENTIO); }
