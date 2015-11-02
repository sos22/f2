#include "err.h"

#include "clustername.H"
#include "compute.H"
#include "eqclient.H"
#include "logging.H"
#include "main.H"
#include "parsers.H"
#include "pubsub.H"

#include "parsers.tmpl"

orerror<void>
f2main(list<string> &args) {
    initpubsub();
    if (args.length() != 2) {
        errx(1, "need two arguments, the cluster and the agent name"); }
    auto cluster(clustername::parser()
                 .match(args.idx(0))
                 .fatal("parsing cluster name " + fields::mk(args.idx(0))));
    auto peer(agentname::parser()
              .match(args.idx(1))
              .fatal("parsing agent name " + fields::mk(args.idx(1))));
    auto pool(connpool::build(cluster).fatal("building conn pool"));

    auto clnt(eqclient<proto::compute::event>::connect(
                  clientio::CLIENTIO,
                  pool,
                  peer,
                  proto::eq::names::compute,
                  timedelta::seconds(30).future())
              .fatal("connecting storage event queue")
              .first());
    while (true) {
        auto p(clnt->pop(clientio::CLIENTIO)
               .fatal("getting event from compute agent"));
        logmsg(loglevel::info, p.field()); } }
