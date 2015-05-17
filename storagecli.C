#include <err.h>

#include "storageclient.H"

#include "pair.tmpl"
#include "parsers.tmpl"

int
main(int argc, char *argv[]) {
    initlogging("storageclient");
    initpubsub();
    if (argc < 4) {
        errx(1, "need at least three arguments: a cluster, a peer and a mode");}
    auto cluster(parsers::__clustername()
                 .match(argv[1])
                 .fatal("parsing cluser name " + fields::mk(argv[1])));
    auto peer(parsers::_agentname()
              .match(argv[2])
              .fatal("parsing agent name " + fields::mk(argv[2])));
    auto pool(connpool::build(cluster).fatal("building conn pool"));
    auto &conn(*storageclient::connect(clientio::CLIENTIO, pool, peer)
               .fatal("connecting to storage agent"));
    if (!strcmp(argv[3], "CREATEJOB")) {
        if (argc != 5) errx(1, "CREATEJOB needs a job name");
        auto job(job::parser()
                 .match(argv[4])
                 .fatal("parsing job " + fields::mk(argv[4])));
        auto m = conn.createjob(clientio::CLIENTIO, job);
        if (m == error::already) m.failure().warn("already created");
        else if (m.isfailure()) m.failure().fatal("creating empty job");
        else fields::print("result: " + fields::mk(m.success()) + "\n"); }
    else if (!strcmp(argv[3], "APPEND")) {
        if (argc != 8) {
            errx(1,
                 "APPEND needs a job, a stream name, the old size, "
                 "and a thing to append"); }
        auto job(jobname::parser()
                 .match(argv[4])
                 .fatal("parsing job name " + fields::mk(argv[4])));
        auto stream(streamname::parser()
                    .match(argv[5])
                    .fatal("parsing stream name " + fields::mk(argv[5])));
        auto oldsize(parsers::_bytecount()
                     .match(argv[6])
                     .fatal("parsing byte count " + fields::mk(argv[6])));
        buffer buf;
        buf.queue(argv[7], strlen(argv[7]));
        conn.append(clientio::CLIENTIO, job, stream, Steal, buf, oldsize)
            .fatal(fields::mk("appending to stream")); }
    else if (!strcmp(argv[3], "FINISH")) {
        if (argc != 6) errx(1, "FINISH needs a job and a stream name");
        auto job(jobname::parser()
                 .match(argv[4])
                 .fatal("parsing job name " + fields::mk(argv[4])));
        auto stream(streamname::parser()
                    .match(argv[5])
                    .fatal("parsing stream name " + fields::mk(argv[5])));
        conn.finish(clientio::CLIENTIO, job, stream)
            .fatal(fields::mk("finishing stream")); }
    else if (!strcmp(argv[3], "READ")) {
        if (argc < 6 || argc > 8) {
            errx(1,
                 "READ needs a job and a stream name, and "
                 "optionally takes a start end end offset"); }
        auto job(jobname::parser()
                 .match(argv[4])
                 .fatal("parsing job name " + fields::mk(argv[4])));
        auto stream(streamname::parser()
                    .match(argv[5])
                    .fatal("parsing stream name " + fields::mk(argv[5])));
        maybe<bytecount> start(Nothing);
        if (argc > 6) {
            start = parsers::_bytecount()
                .match(argv[6])
                .fatal("parsing start offset " + fields::mk(argv[6])); }
        maybe<bytecount> end(Nothing);
        if (argc > 7) {
            end = parsers::_bytecount()
                .match(argv[7])
                .fatal("parsing end offset " + fields::mk(argv[7])); }
        auto r(conn.read(clientio::CLIENTIO, job, stream, start, end)
               .fatal("reading stream"));
        fields::print("size " + r.first().field() + "\n");;
        fields::print("content " +
                      fields::mk(r.second()).showshape().words() +
                      "\n"); }
    else if (strcmp(argv[3], "LISTJOBS") == 0) {
        if (argc != 4) errx(1,"LISTJOBS takes no arguments");
        auto r(conn.listjobs(clientio::CLIENTIO)
               .fatal("listing jobs"));
        fields::print(fields::mk(r) + "\n"); }
    else if (strcmp(argv[3], "STATJOB") == 0) {
        if (argc != 5) errx(1,"STATJOB takes jobname argument only");
        jobname j(jobname::parser()
                  .match(argv[4])
                  .fatal("parsing jobname " + fields::mk(argv[4])));
        fields::print(fields::mk(conn.statjob(clientio::CLIENTIO, j)
                                 .fatal("statting job " + fields::mk(j))) +
                      "\n"); }
    else if (strcmp(argv[3], "LISTSTREAMS") == 0) {
        if (argc != 5) errx(1, "LISTSTREAMS takes a job name");
        auto job(jobname::parser()
                 .match(argv[4])
                 .fatal("parsing job name " + fields::mk(argv[4])));
        auto r(conn.liststreams(clientio::CLIENTIO, job)
               .fatal("listing streams"));
        fields::print(fields::mk(r) + "\n"); }
    else if (strcmp(argv[3], "STATSTREAM") == 0) {
        if (argc != 6) {
            errx(1, "STATSTREAM takes a job name and a stream name"); }
        auto job(jobname::parser()
                 .match(argv[4])
                 .fatal("parsing job name " + fields::mk(argv[4])));
        auto stream(streamname::parser()
                    .match(argv[5])
                    .fatal("parsing stream name " + fields::mk(argv[5])));
        fields::print(
            "result: " +
            fields::mk(conn.statstream(clientio::CLIENTIO, job, stream)
                       .fatal(fields::mk("statting stream"))) +
            "\n"); }
    else if (strcmp(argv[3], "REMOVEJOB") == 0) {
        if (argc != 5) errx(1, "REMOVEJOB takes a jobname argument");
        auto job(jobname::parser()
                 .match(argv[4])
                 .fatal("parsing job name " + fields::mk(argv[4])));
        conn.removejob(clientio::CLIENTIO, job)
            .fatal(fields::mk("removing job")); }
    else errx(1, "unknown mode %s", argv[3]);

    pool->destroy();
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging(); }
