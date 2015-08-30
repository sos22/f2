#include <err.h>

#include "agentname.H"
#include "clustername.H"
#include "compute.H"
#include "computeclient.H"
#include "connpool.H"
#include "eq.H"
#include "fields.H"
#include "logging.H"
#include "parsers.H"
#include "peername.H"
#include "pubsub.H"
#include "job.H"
#include "jobname.H"

#include "fields.tmpl"
#include "list.tmpl"
#include "maybe.tmpl"
#include "parsers.tmpl"

int
main(int argc, char *argv[]) {
    initlogging("computeclient");
    initpubsub();
    if (argc < 4) {
        errx(1, "need at least three arguments: a cluster, a peer and a mode");}
    auto cluster(parsers::__clustername()
                 .match(argv[1])
                 .fatal("parsing cluster name " + fields::mk(argv[1])));
    auto peer(parsers::_agentname()
              .match(argv[2])
              .fatal("parsing agent name " + fields::mk(argv[2])));
    auto &pool(*connpool::build(cluster).fatal("building conn pool"));
    auto &client(computeclient::connect(pool, peer));
    if (!strcmp(argv[3], "START")) {
        if (argc != 5) errx(1, "START mode needs a job argument");
        auto j(job::parser()
               .match(argv[4])
               .fatal("parsing job " + fields::mk(argv[4])));
        fields::print("result " +
                      client
                      .start(clientio::CLIENTIO, j)
                      .fatal("starting job")
                      .field()); }
    else if (!strcmp(argv[3], "ENUMERATE")) {
        if (argc != 4) errx(1, "ENUMERATE has no arguments");
        fields::print("result " +
                      client
                      .enumerate(clientio::CLIENTIO)
                      .fatal("enumerating jobs")
                      .field()); }
    else if (!strcmp(argv[3], "DROP")) {
        if (argc != 5) {
            errx(1, "DROP mode takes a single argument, the job name to drop");}
        auto j(jobname::parser()
               .match(argv[4])
               .fatal("parsing job " + fields::mk(argv[4])));
        client
            .drop(clientio::CLIENTIO, j)
            .fatal("dropping job"); }
    else {
        errx(1, "unknown mode %s", argv[3]); }
    client.destroy();
    pool.destroy();
    deinitpubsub(clientio::CLIENTIO); }
