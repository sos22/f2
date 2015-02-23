#include "err.h"

#include "clustername.H"
#include "compute.H"
#include "eqclient.H"
#include "logging.H"
#include "parsers.H"
#include "pubsub.H"

#include "parsers.tmpl"

int
main(int argc, char *argv[]) {
    initlogging("computeclient");
    initpubsub();
    if (argc != 3) {
        errx(1, "need two arguments, the cluster and the agent name"); }
    auto cluster(parsers::__clustername()
                 .match(argv[1])
                 .fatal("parsing cluster name " + fields::mk(argv[1])));
    auto peer(parsers::_agentname()
              .match(argv[2])
              .fatal("parsing agent name " + fields::mk(argv[2])));
    auto pool(connpool::build(cluster).fatal("building conn pool"));

    auto clnt(eqclient<proto::compute::event>::connect(
                  clientio::CLIENTIO,
                  pool,
                  peer,
                  proto::eq::names::compute,
                  timedelta::seconds(30).future())
              .fatal("connecting storage event queue"));
    while (true) {
        auto p(clnt->pop(clientio::CLIENTIO)
               .fatal("getting event from compute agent"));
        logmsg(loglevel::info, p.field()); } }
