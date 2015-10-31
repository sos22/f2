#include <err.h>

#include "agentname.H"
#include "clustername.H"
#include "compute.H"
#include "computeclient.H"
#include "connpool.H"
#include "eq.H"
#include "fields.H"
#include "logging.H"
#include "main.H"
#include "parsers.H"
#include "peername.H"
#include "pubsub.H"
#include "job.H"
#include "jobname.H"

#include "fields.tmpl"
#include "list.tmpl"
#include "maybe.tmpl"
#include "parsers.tmpl"

orerror<void>
f2main(list<string> &args) {
    initpubsub();
    if (args.length() < 3) {
        errx(1, "need at least three arguments: a cluster, a peer and a mode");}
    auto cluster(parsers::__clustername()
                 .match(args.idx(0))
                 .fatal("parsing cluster name " + fields::mk(args.idx(0))));
    auto peer(agentname::parser()
              .match(args.idx(1))
              .fatal("parsing agent name " + fields::mk(args.idx(1))));
    auto &pool(*connpool::build(cluster).fatal("building conn pool"));
    auto &client(computeclient::connect(pool, peer));
    if (args.idx(2) == "START") {
        if (args.length() != 4) errx(1, "START mode needs a job argument");
        auto j(job::parser()
               .match(args.idx(3))
               .fatal("parsing job " + fields::mk(args.idx(3))));
        fields::print("result " +
                      client
                      .start(clientio::CLIENTIO, j)
                      .fatal("starting job")
                      .field()); }
    else if (args.idx(2) == "ENUMERATE") {
        if (args.length() != 3) errx(1, "ENUMERATE has no arguments");
        fields::print("result " +
                      client
                      .enumerate(clientio::CLIENTIO)
                      .fatal("enumerating jobs")
                      .field()); }
    else if (args.idx(2) == "DROP") {
        if (args.length() != 4) {
            errx(1, "DROP mode takes a single argument, the job name to drop");}
        auto j(jobname::parser()
               .match(args.idx(3))
               .fatal("parsing job " + fields::mk(args.idx(3))));
        client
            .drop(clientio::CLIENTIO, j)
            .fatal("dropping job"); }
    else {
        errx(1, "unknown mode %s", args.idx(2).c_str()); }
    client.destroy();
    pool.destroy();
    deinitpubsub(clientio::CLIENTIO);
    return Success; }
