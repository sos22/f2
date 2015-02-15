#include <err.h>

#include "connpool.H"
#include "coordinator.H"
#include "jobname.H"
#include "streamname.H"
#include "streamstatus.H"

#include "list.tmpl"
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
             "clustername, slavename, and mode"); }
    auto cluster(parsers::__clustername()
                 .match(argv[1])
                 .fatal("parsing " + fields::mk(argv[1]) +
                        " as clustername"));
    auto sn(parsers::_slavename()
            .match(argv[2])
            .fatal("parsing " + fields::mk(argv[2]) +
                   " as slavename"));
    auto pool(connpool::build(cluster).fatal("building connection pool"));
    if (!strcmp(argv[3], "FINDJOB")) {
        if (argc != 5) errx(1, "FINDJOB needs a jobname argument");
        auto jn(parsers::_jobname()
                .match(argv[4])
                .fatal("parsing " + fields::mk(argv[4]) + " as jobname"));
        maybe<list<slavename> > res(Nothing);
        pool->call(clientio::CLIENTIO,
                   sn,
                   interfacetype::coordinator,
                   Nothing,
                   [&jn] (serialise1 &s, connpool::connlock) {
                       s.push(proto::coordinator::tag::findjob);
                       s.push(jn); },
                   [&res] (deserialise1 &ds, connpool::connlock) {
                       res.mkjust(ds);
                       return ds.status(); })
            .fatal("making FINDJOB call");
        fields::print("result " + fields::mk(res.just()) + "\n"); }
    else if (!strcmp(argv[3], "FINDSTREAM")) {
        if (argc != 6) errx(1,"FINDJOB needs jobname and streamname arguments");
        auto jn(parsers::_jobname()
                .match(argv[4])
                .fatal("parsing " + fields::mk(argv[4]) + " as jobname"));
        auto str(parsers::_streamname()
                 .match(argv[5])
                 .fatal("parsing " + fields::mk(argv[5]) + " as streamname"));
        maybe<list<pair<slavename, streamstatus> > > res(Nothing);
        pool->call(clientio::CLIENTIO,
                   sn,
                   interfacetype::coordinator,
                   Nothing,
                   [&jn, &str] (serialise1 &s, connpool::connlock) {
                       s.push(proto::coordinator::tag::findstream);
                       s.push(jn);
                       s.push(str); },
                   [&res] (deserialise1 &ds, connpool::connlock) {
                       res.mkjust(ds);
                       return ds.status(); })
            .fatal("making FINDSTREAM call");
        fields::print("result " + fields::mk(res.just()) + "\n"); }
    else {
        errx(1, "unknown mode %s", argv[3]); }
    pool->destroy();
    deinitpubsub(clientio::CLIENTIO); }
