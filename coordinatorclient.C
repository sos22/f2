#include <err.h>

#include "connpool.H"
#include "coordinator.H"
#include "job.H"

#include "maybe.tmpl"
#include "parsers.tmpl"

#include "fieldfinal.H"

int
main(int argc, char *argv[]) {
    initpubsub();
    initlogging("coordinatorclient");
    if (argc < 4) {
        errx(1,
             "need at least three arguments: "
             "clustername, agentname, and mode"); }
    auto cluster(parsers::__clustername()
                 .match(argv[1])
                 .fatal("parsing " + fields::mk(argv[1]) +
                        " as clustername"));
    auto sn(parsers::_agentname()
            .match(argv[2])
            .fatal("parsing " + fields::mk(argv[2]) +
                   " as agentname"));
    auto pool(connpool::build(cluster).fatal("building connection pool"));
    if (!strcmp(argv[3], "CREATEJOB")) {
        if (argc != 5) errx(1, "CREATEJOB needs a job argument");
        auto j(job::parser()
               .match(argv[4])
               .fatal("parsing " + fields::mk(argv[4]) +
                      " as a job descriptor"));
        maybe<agentname> res(Nothing);
        pool->call(clientio::CLIENTIO,
                   sn,
                   interfacetype::coordinator,
                   Nothing,
                   [&j] (serialise1 &s, connpool::connlock) {
                       s.push(proto::coordinator::tag::createjob);
                       s.push(j); },
                   [&res] (deserialise1 &ds, connpool::connlock) {
                       res.mkjust(ds);
                       return ds.status(); })
            .fatal("making CREATEJOB call");
        fields::print("result " + fields::mk(res.just()) + "\n"); }
    else {
        errx(1, "unknown mode %s", argv[3]); }
    pool->destroy();
    deinitpubsub(clientio::CLIENTIO); }
