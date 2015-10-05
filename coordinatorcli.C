#include <err.h>

#include "connpool.H"
#include "coordinator.H"
#include "job.H"
#include "main.H"

#include "connpool.tmpl"
#include "maybe.tmpl"
#include "parsers.tmpl"

orerror<void>
f2main(list<string> &args) {
    initpubsub();
    if (args.length() < 3) {
        errx(1,
             "need at least three arguments: "
             "clustername, agentname, and mode"); }
    auto cluster(parsers::__clustername()
                 .match(args.idx(0))
                 .fatal("parsing " + fields::mk(args.idx(0)) +
                        " as clustername"));
    auto sn(parsers::_agentname()
            .match(args.idx(1))
            .fatal("parsing " + fields::mk(args.idx(1)) +
                   " as agentname"));
    auto pool(connpool::build(cluster).fatal("building connection pool"));
    if (args.idx(2) == "CREATEJOB") {
        if (args.length() != 4) errx(1, "CREATEJOB needs a job argument");
        auto j(job::parser()
               .match(args.idx(3))
               .fatal("parsing " + fields::mk(args.idx(3)) +
                      " as a job descriptor"));
        auto res(
            pool->call<agentname>(
                clientio::CLIENTIO,
                sn,
                interfacetype::coordinator,
                Nothing,
                [&j] (serialise1 &s, connpool::connlock) {
                    s.push(proto::coordinator::tag::createjob);
                    s.push(j); },
                [] (deserialise1 &ds, connpool::connlock) {
                    return agentname(ds); })
            .fatal("making CREATEJOB call"));
        fields::print("result " + fields::mk(res) + "\n"); }
    else {
        errx(1, "unknown mode %s", args.idx(2).c_str()); }
    pool->destroy();
    deinitpubsub(clientio::CLIENTIO);
    return Success; }
