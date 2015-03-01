#include "err.h"

#include "agentname.H"
#include "clustername.H"
#include "connpool.H"
#include "filesystemproto.H"
#include "jobname.H"
#include "logging.H"
#include "pubsub.H"
#include "streamname.H"
#include "streamstatus.H"

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
    
    auto makecall(
        [pool, &sn]
        (proto::filesystem::tag tag,
         const std::function<void (serialise1 &)> &serialise,
         const std::function<void (deserialise1 &)> &deserialise) {
            pool->call(clientio::CLIENTIO,
                       sn,
                       interfacetype::filesystem,
                       Nothing,
                       [&serialise, tag] (serialise1 &s, connpool::connlock) {
                           s.push(tag);
                           serialise(s); },
                       [&deserialise] (deserialise1 &ds, connpool::connlock) {
                           deserialise(ds);
                           return ds.status(); })
                .fatal("making call against filesystem agent"); });
    
    if (!strcmp(argv[3], "FINDJOB")) {
        if (argc != 5) errx(1, "FINDJOB needs a jobname argument");
        auto jn(parsers::_jobname()
                .match(argv[4])
                .fatal("parsing " + fields::mk(argv[4]) + " as jobname"));
        maybe<list<agentname> > res(Nothing);
        makecall(proto::filesystem::tag::findjob,
                 [&jn] (serialise1 &s) { s.push(jn); },
                 [&res] (deserialise1 &ds) { res.mkjust(ds); });
        fields::print("result " + fields::mk(res.just()) + "\n"); }
    else if (!strcmp(argv[3], "FINDSTREAM")) {
        if (argc != 6) errx(1,"FINDJOB needs jobname and streamname arguments");
        auto jn(parsers::_jobname()
                .match(argv[4])
                .fatal("parsing " + fields::mk(argv[4]) + " as jobname"));
        auto str(parsers::_streamname()
                 .match(argv[5])
                 .fatal("parsing " + fields::mk(argv[5]) + " as streamname"));
        maybe<list<pair<agentname, streamstatus> > > res(Nothing);
        makecall(proto::filesystem::tag::findstream,
                 [&jn, &str] (serialise1 &s) {
                     s.push(jn);
                     s.push(str); },
                 [&res] (deserialise1 &ds) { res.mkjust(ds); });
        fields::print("result " + fields::mk(res.just()) + "\n"); }
    else if (!strcmp(argv[3], "NOMINATEAGENT")) {
        if (argc != 4 && argc != 5) {
            errx(1, "NOMINATEAGENT takes an optional jobname argument"); }
        maybe<jobname> jn(Nothing);
        if (argc == 5) {
            jn = parsers::_jobname()
                .match(argv[4])
                .fatal("parsing " + fields::mk(argv[4]) + " as jobname"); }
        maybe<maybe<agentname> > res(Nothing);
        makecall(proto::filesystem::tag::nominateagent,
                 [&jn] (serialise1 &s) { s.push(jn); },
                 [&res] (deserialise1 &ds) { res.mkjust(ds); });
        fields::print("result " + fields::mk(res.just()) + "\n"); }
    else {
        errx(1, "unknown mode %s", argv[3]); }
    pool->destroy();
    deinitpubsub(clientio::CLIENTIO); }
