#include "err.h"

#include "agentname.H"
#include "clustername.H"
#include "compute.H"
#include "connpool.H"
#include "eq.H"
#include "fields.H"
#include "logging.H"
#include "parsers.H"
#include "peername.H"
#include "pubsub.H"
#include "job.H"
#include "jobname.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "parsers.tmpl"

#include "fieldfinal.H"

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
    auto pool(connpool::build(cluster).fatal("building conn pool"));
    if (!strcmp(argv[3], "START")) {
        if (argc != 5) errx(1, "START mode needs a job argument");
        auto j(parsers::_job()
               .match(argv[4])
               .fatal("parsing job " + fields::mk(argv[4])));
        maybe<proto::eq::eventid> eid(Nothing);
        maybe<proto::compute::tasktag> tag(Nothing);
        pool->call(clientio::CLIENTIO,
                   peer,
                   interfacetype::compute,
                   Nothing,
                   [&j] (serialise1 &s, connpool::connlock) {
                       s.push(proto::compute::tag::start);
                       s.push(j); },
                   [&eid, &tag] (deserialise1 &ds, connpool::connlock) {
                       eid.mkjust(ds);
                       tag.mkjust(ds);
                       return ds.status(); })
            .fatal("calling compute agent START method");
        fields::print("result " + tag.just().field() +
                      " at " + eid.just().field() + "\n"); }
    else if (!strcmp(argv[3], "ENUMERATE")) {
        if (argc != 4) errx(1, "ENUMERATE has no arguments");
        maybe<proto::eq::eventid> eid(Nothing);
        maybe<list<proto::compute::jobstatus> > res(Nothing);
        pool->call(clientio::CLIENTIO,
                   peer,
                   interfacetype::compute,
                   Nothing,
                   [] (serialise1 &s, connpool::connlock) {
                       s.push(proto::compute::tag::enumerate); },
                   [&eid, &res] (deserialise1 &ds, connpool::connlock) {
                       eid.mkjust(ds);
                       res.mkjust(ds);
                       return ds.status(); })
            .fatal("calling compute agent ENUMERATE method");
        fields::print("result " + fields::mk(res.just()) +
                      " at " + eid.just().field() + "\n"); }
    else if (!strcmp(argv[3], "DROP")) {
        if (argc != 5) {
            errx(1, "DROP mode takes a single argument, the job name to drop");}
        auto j(jobname::parser()
               .match(argv[4])
               .fatal("parsing job " + fields::mk(argv[4])));
        pool->call(clientio::CLIENTIO,
                   peer,
                   interfacetype::compute,
                   Nothing,
                   [&j] (serialise1 &s, connpool::connlock) {
                       s.push(proto::compute::tag::drop);
                       s.push(j); },
                   [] (deserialise1 &ds, connpool::connlock) {
                       return ds.status(); })
            .fatal("calling compute agent DROP method"); }
    else {
        errx(1, "unknown mode %s", argv[3]); }
    pool->destroy();
    deinitpubsub(clientio::CLIENTIO); }
