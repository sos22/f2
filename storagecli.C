#include <err.h>

#include "main.H"
#include "storageclient.H"

#include "list.tmpl"
#include "pair.tmpl"
#include "parsers.tmpl"

orerror<void>
f2main(list<string> &args) {
    initpubsub();
    if (args.length() < 3) {
        errx(1, "need at least three arguments: a cluster, a peer and a mode");}
    auto cluster(parsers::__clustername()
                 .match(args.idx(0))
                 .fatal("parsing cluser name " + fields::mk(args.idx(0))));
    auto peer(parsers::_agentname()
              .match(args.idx(1))
              .fatal("parsing agent name " + fields::mk(args.idx(1))));
    auto pool(connpool::build(cluster).fatal("building conn pool"));
    auto &conn(storageclient::connect(pool, peer));
    if (args.idx(2) == "CREATEJOB") {
        if (args.length() != 4) errx(1, "CREATEJOB needs a job name");
        auto job(job::parser()
                 .match(args.idx(3))
                 .fatal("parsing job " + fields::mk(args.idx(3))));
        auto m = conn.createjob(clientio::CLIENTIO, job);
        if (m == error::already) m.failure().warn("already created");
        else if (m.isfailure()) m.failure().fatal("creating empty job");
        else fields::print("result: " + fields::mk(m.success()) + "\n"); }
    else if (args.idx(2) == "APPEND") {
        if (args.length() != 7) {
            errx(1,
                 "APPEND needs a job, a stream name, the old size, "
                 "and a thing to append"); }
        auto job(jobname::parser()
                 .match(args.idx(3))
                 .fatal("parsing job name " + fields::mk(args.idx(3))));
        auto stream(streamname::parser()
                    .match(args.idx(4))
                    .fatal("parsing stream name " + fields::mk(args.idx(4))));
        auto oldsize(parsers::_bytecount()
                     .match(args.idx(5))
                     .fatal("parsing byte count " + fields::mk(args.idx(5))));
        buffer buf;
        buf.queue(args.idx(6).c_str(), args.idx(6).len());
        conn.append(clientio::CLIENTIO, job, stream, Steal, buf, oldsize)
            .fatal(fields::mk("appending to stream")); }
    else if (args.idx(2) == "FINISH") {
        if (args.length() != 5) errx(1, "FINISH needs a job and a stream name");
        auto job(jobname::parser()
                 .match(args.idx(3))
                 .fatal("parsing job name " + fields::mk(args.idx(3))));
        auto stream(streamname::parser()
                    .match(args.idx(4))
                    .fatal("parsing stream name " + fields::mk(args.idx(4))));
        conn.finish(clientio::CLIENTIO, job, stream)
            .fatal(fields::mk("finishing stream")); }
    else if (args.idx(2) == "READ") {
        if (args.length() < 5 || args.length() > 7) {
            errx(1,
                 "READ needs a job and a stream name, and "
                 "optionally takes a start end end offset"); }
        auto job(jobname::parser()
                 .match(args.idx(3))
                 .fatal("parsing job name " + fields::mk(args.idx(3))));
        auto stream(streamname::parser()
                    .match(args.idx(4))
                    .fatal("parsing stream name " + fields::mk(args.idx(4))));
        maybe<bytecount> start(Nothing);
        if (args.length() > 5) {
            start = parsers::_bytecount()
                .match(args.idx(5))
                .fatal("parsing start offset " + fields::mk(args.idx(5))); }
        maybe<bytecount> end(Nothing);
        if (args.length() > 6) {
            end = parsers::_bytecount()
                .match(args.idx(6))
                .fatal("parsing end offset " + fields::mk(args.idx(6))); }
        auto r(conn.read(clientio::CLIENTIO, job, stream, start, end)
               .fatal("reading stream"));
        fields::print("size " + r.first().field() + "\n");;
        fields::print("content " +
                      fields::mk(r.second()).showshape().words() +
                      "\n"); }
    else if (args.idx(2) == "LISTJOBS") {
        if (args.length() != 3) errx(1,"LISTJOBS takes no arguments");
        auto r(conn.listjobs(clientio::CLIENTIO)
               .fatal("listing jobs"));
        fields::print(fields::mk(r) + "\n"); }
    else if (args.idx(2) == "STATJOB") {
        if (args.length() != 4) errx(1,"STATJOB takes jobname argument only");
        jobname j(jobname::parser()
                  .match(args.idx(3))
                  .fatal("parsing jobname " + fields::mk(args.idx(3))));
        fields::print(fields::mk(conn.statjob(clientio::CLIENTIO, j)
                                 .fatal("statting job " + fields::mk(j))) +
                      "\n"); }
    else if (args.idx(2) == "LISTSTREAMS") {
        if (args.length() != 4) errx(1, "LISTSTREAMS takes a job name");
        auto job(jobname::parser()
                 .match(args.idx(3))
                 .fatal("parsing job name " + fields::mk(args.idx(3))));
        auto r(conn.liststreams(clientio::CLIENTIO, job)
               .fatal("listing streams"));
        fields::print(fields::mk(r) + "\n"); }
    else if (args.idx(2) == "STATSTREAM") {
        if (args.length() != 5) {
            errx(1, "STATSTREAM takes a job name and a stream name"); }
        auto job(jobname::parser()
                 .match(args.idx(3))
                 .fatal("parsing job name " + fields::mk(args.idx(3))));
        auto stream(streamname::parser()
                    .match(args.idx(4))
                    .fatal("parsing stream name " + fields::mk(args.idx(4))));
        fields::print(
            "result: " +
            fields::mk(conn.statstream(clientio::CLIENTIO, job, stream)
                       .fatal(fields::mk("statting stream"))) +
            "\n"); }
    else if (args.idx(2) == "REMOVEJOB") {
        if (args.length() != 4) errx(1, "REMOVEJOB takes a jobname argument");
        auto job(jobname::parser()
                 .match(args.idx(3))
                 .fatal("parsing job name " + fields::mk(args.idx(3))));
        conn.removejob(clientio::CLIENTIO, job)
            .fatal(fields::mk("removing job")); }
    else errx(1, "unknown mode %s", args.idx(2).c_str());

    pool->destroy();
    deinitpubsub(clientio::CLIENTIO);
    return Success; }
