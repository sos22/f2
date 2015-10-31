#include "err.h"

#include "agentname.H"
#include "clustername.H"
#include "connpool.H"
#include "eq.H"
#include "filesystemproto.H"
#include "jobname.H"
#include "logging.H"
#include "main.H"
#include "pubsub.H"
#include "streamname.H"
#include "streamstatus.H"

#include "connpool.tmpl"
#include "fields.tmpl"
#include "list.tmpl"
#include "maybe.tmpl"
#include "pair.tmpl"
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
    auto sn(agentname::parser()
            .match(args.idx(1))
            .fatal("parsing " + fields::mk(args.idx(1)) +
                   " as agentname"));
    auto pool(connpool::build(cluster).fatal("building connection pool"));
    
    auto makecall(
        [pool, &sn]
        (proto::filesystem::tag tag,
         const std::function<void (serialise1 &)> &serialise,
         const std::function<void (deserialise1 &)> &deserialise) {
            pool->call<void>(
                clientio::CLIENTIO,
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
    
    if (args.idx(2) == "FINDJOB") {
        if (args.length() != 4) errx(1, "FINDJOB needs a jobname argument");
        auto jn(jobname::parser()
                .match(args.idx(3))
                .fatal("parsing " + fields::mk(args.idx(3)) + " as jobname"));
        maybe<list<agentname> > res(Nothing);
        makecall(proto::filesystem::tag::findjob,
                 [&jn] (serialise1 &s) { s.push(jn); },
                 [&res] (deserialise1 &ds) { res.mkjust(ds); });
        fields::print("result " + fields::mk(res.just()) + "\n"); }
    else if (args.idx(2) == "FINDSTREAM") {
        if (args.length() != 5) {
            errx(1,"FINDJOB needs jobname and streamname arguments"); }
        auto jn(jobname::parser()
                .match(args.idx(3))
                .fatal("parsing " + fields::mk(args.idx(3)) + " as jobname"));
        auto str(streamname::parser()
                 .match(args.idx(4))
                 .fatal("parsing " + fields::mk(args.idx(4)) +
                        " as streamname"));
        maybe<list<pair<agentname, streamstatus> > > res(Nothing);
        makecall(proto::filesystem::tag::findstream,
                 [&jn, &str] (serialise1 &s) {
                     s.push(jn);
                     s.push(str); },
                 [&res] (deserialise1 &ds) { res.mkjust(ds); });
        fields::print("result " + fields::mk(res.just()) + "\n"); }
    else if (args.idx(2) == "NOMINATEAGENT") {
        if (args.length() != 3 && args.length() != 4) {
            errx(1, "NOMINATEAGENT takes an optional jobname argument"); }
        maybe<jobname> jn(Nothing);
        if (args.length() == 4) {
            jn = jobname::parser()
                .match(args.idx(3))
                .fatal("parsing " + fields::mk(args.idx(3)) + " as jobname"); }
        maybe<maybe<agentname> > res(Nothing);
        makecall(proto::filesystem::tag::nominateagent,
                 [&jn] (serialise1 &s) { s.push(jn); },
                 [&res] (deserialise1 &ds) { res.mkjust(ds); });
        fields::print("result " + fields::mk(res.just()) + "\n"); }
    else if (args.idx(2) == "STORAGEBARRIER") {
        if (args.length() != 5) {
            errx(1, "STORAGEBARRIER needs agentname and event ID arguments"); }
        auto an(agentname::parser()
                .match(args.idx(3))
                .fatal("parsing " + fields::mk(args.idx(3)) + " as agentname"));
        auto eid(parsers::eq::eventid()
                 .match(args.idx(4))
                 .fatal("parsing " + fields::mk(args.idx(4)) + " as event ID"));
        makecall(proto::filesystem::tag::storagebarrier,
                 [&an, &eid] (serialise1 &s) {
                     s.push(an);
                     s.push(eid); },
                 [] (deserialise1 &) {}); }
    else {
        errx(1, "unknown mode %s", args.idx(2).c_str()); }
    pool->destroy();
    deinitpubsub(clientio::CLIENTIO);
    return Success; }
