#include "err.h"

#include "agentname.H"
#include "clustername.H"
#include "connpool.H"
#include "filesystemproto.H"
#include "jobname.H"
#include "logging.H"
#include "pubsub.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "parsers.tmpl"

#include "fieldfinal.H"

int
main(int argc, char *argv[]) {
    initpubsub();
    initlogging("filesystemclient");
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
    if (!strcmp(argv[3], "FINDJOB")) {
        if (argc != 5) errx(1, "FINDJOB needs a jobname argument");
        auto jn(parsers::_jobname()
                .match(argv[4])
                .fatal("parsing " + fields::mk(argv[4]) + " as jobname"));
        maybe<list<agentname> > res(Nothing);
        pool->call(clientio::CLIENTIO,
                   sn,
                   interfacetype::filesystem,
                   Nothing,
                   [&jn] (serialise1 &s, connpool::connlock) {
                       s.push(proto::filesystem::tag::findjob);
                       s.push(jn); },
                   [&res] (deserialise1 &ds, connpool::connlock) {
                       res.mkjust(ds);
                       return ds.status(); })
            .fatal("making FINDJOB call");
        fields::print("result " + fields::mk(res.just()) + "\n"); }
    else {
        errx(1, "unknown mode %s", argv[3]); }
    pool->destroy();
    deinitpubsub(clientio::CLIENTIO); }
