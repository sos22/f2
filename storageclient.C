#include <err.h>
#include <stdio.h>
#include <unistd.h>

#include "buffer.H"
#include "clientio.H"
#include "fields.H"
#include "jobname.H"
#include "logging.H"
#include "parsers.H"
#include "peername.H"
#include "pubsub.H"
#include "registrationsecret.H"
#include "rpcconn.H"
#include "streamname.H"
#include "streamstatus.H"

#include "parsers.tmpl"
#include "rpcconn.tmpl"
#include "wireproto.tmpl"

#include "fieldfinal.H"

int
main(int argc, char *argv[]) {
    initlogging("storageclient");
    initpubsub();
    if (argc < 4) {
        errx(1, "need at least three arguments: a secret, a peer, and a mode");}
    auto rs(registrationsecret::parse(argv[1]));
    if (rs.isfailure()) {
        rs.failure().fatal("parsing registration secret " +
                           fields::mk(argv[1])); }
    auto peer(peername::parse(argv[2]));
    if (peer.isfailure()) {
        peer.failure().fatal("parsing peername " +
                             fields::mk(argv[2])); }
    auto conn(rpcconn::connectslave<rpcconn>(
                  clientio::CLIENTIO,
                  peer.success(),
                  rs.success()));
    if (conn.isfailure()) {
        conn.failure().fatal("connecting to " + fields::mk(peer.success())); }
    if (!strcmp(argv[3], "STALL")) {
        if (argc != 4) errx(1, "STALL takes no additional arguments");
        /* Just hang around for a bit so that we can see if PING is
         * working. */
        sleep(10);
    } else if (!strcmp(argv[3], "CREATEEMPTY")) {
        if (argc != 6) {
            errx(1, "CREATEEMPTY needs a job and stream name"); }
        auto job(parsers::_jobname()
                 .match(argv[4])
                 .fatal("parsing job name " + fields::mk(argv[4])));
        auto stream(parsers::_streamname()
                    .match(argv[5])
                    .fatal("parsing stream name " + fields::mk(argv[5])));
        auto m = conn.success()->call(
            clientio::CLIENTIO,
            wireproto::req_message(proto::CREATEEMPTY::tag,
                                   conn.success()->allocsequencenr())
            .addparam(proto::CREATEEMPTY::req::job, job)
            .addparam(proto::CREATEEMPTY::req::stream, stream));
        if (m == error::already) {
            m.failure().warn("already created");
        } else if (m.isfailure()) {
            m.failure().fatal("creating empty file");
        } else {
            delete m.success(); }
    } else if (!strcmp(argv[3], "APPEND")) {
        if (argc != 7) {
            errx(1,
                 "APPEND needs a job, a stream name, and a thing to append"); }
        auto job(parsers::_jobname()
                 .match(argv[4])
                 .fatal("parsing job name " + fields::mk(argv[4])));
        auto stream(parsers::_streamname()
                    .match(argv[5])
                    .fatal("parsing stream name " + fields::mk(argv[5])));
        buffer buf;
        buf.queue(argv[6], strlen(argv[6]));
        auto m = conn.success()->call(
            clientio::CLIENTIO,
            wireproto::req_message(proto::APPEND::tag,
                                   conn.success()->allocsequencenr())
            .addparam(proto::APPEND::req::job, job)
            .addparam(proto::APPEND::req::stream, stream)
            .addparam(proto::APPEND::req::bytes, buf))
            .fatal(fields::mk("appending to stream"));
        delete m;
    } else if (!strcmp(argv[3], "FINISH")) {
        if (argc != 6) {
            errx(1, "FINISH needs a job and a stream name"); }
        auto job(parsers::_jobname()
                 .match(argv[4])
                 .fatal("parsing job name " + fields::mk(argv[4])));
        auto stream(parsers::_streamname()
                    .match(argv[5])
                    .fatal("parsing stream name " + fields::mk(argv[5])));
        auto m = conn.success()->call(
            clientio::CLIENTIO,
            wireproto::req_message(proto::FINISH::tag,
                                   conn.success()->allocsequencenr())
            .addparam(proto::FINISH::req::job, job)
            .addparam(proto::FINISH::req::stream, stream))
            .fatal(fields::mk("finishing stream"));
        delete m;
    } else if (!strcmp(argv[3], "READ")) {
        if (argc < 6 || argc > 8) {
            errx(1,
                 "READ needs a job and a stream name, and "
                 "optionally takes a start end end offset"); }
        auto job(parsers::_jobname()
                 .match(argv[4])
                 .fatal("parsing job name " + fields::mk(argv[4])));
        auto stream(parsers::_streamname()
                    .match(argv[5])
                    .fatal("parsing stream name " + fields::mk(argv[5])));
        wireproto::req_message req(proto::READ::tag,
                                   conn.success()->allocsequencenr());
        req.addparam(proto::READ::req::job, job);
        req.addparam(proto::READ::req::stream, stream);
        if (argc > 6) {
            req.addparam(
                proto::READ::req::start,
                parsers::intparser<unsigned long>()
                .match(argv[6])
                .fatal("parsing start offset " + fields::mk(argv[6]))); }
        if (argc > 7) {
            req.addparam(
                proto::READ::req::end,
                parsers::intparser<unsigned long>()
                .match(argv[7])
                .fatal("parsing end offset " + fields::mk(argv[7]))); }
        auto m = conn.success()->call(clientio::CLIENTIO, req)
            .fatal(fields::mk("reading stream"));
        fields::print("size " +
                      fields::mk(
                          m->getparam(proto::READ::resp::size)
                          .fatal(fields::mk("response missing stream size")))
                      + "\n");
        fields::print("content " +
                      fields::mk(
                          m->getparam(proto::READ::resp::bytes)
                          .fatal(fields::mk("response missing stream content")))
                      .showshape()
                      .words()
                      + "\n");
        delete m;
    } else if (strcmp(argv[3], "LISTJOBS") == 0) {
        if (argc > 6) {
            errx(1,"LISTJOBS takes optional cursor and limit arguments only"); }
        wireproto::req_message req(proto::LISTJOBS::tag,
                                   conn.success()->allocsequencenr());
        if (argc > 4) {
            req.addparam(proto::LISTJOBS::req::cursor,
                         parsers::_jobname()
                         .match(argv[4])
                         .fatal("parsing job name " + fields::mk(argv[4]))); }
        if (argc > 5) {
            req.addparam(proto::LISTJOBS::req::limit,
                         parsers::intparser<unsigned>()
                         .match(argv[5])
                         .fatal("parsing limit " + fields::mk(argv[5]))); }
        auto m = conn.success()->call(clientio::CLIENTIO, req)
            .fatal(fields::mk("cannot list jobs"));
        fields::print("cursor: " +
                      fields::mk(m->getparam(proto::LISTJOBS::resp::cursor)) +
                      "\n");
        list<jobname> jobs;
        m->fetch(proto::LISTJOBS::resp::jobs, jobs)
            .fatal("getting jobs list");
        fields::print("jobs: " + fields::mk(jobs) + "\n");
        jobs.flush();
    } else if (strcmp(argv[3], "LISTSTREAMS") == 0) {
        if (argc < 5 || argc > 7) {
            errx(1,
                 "LISTSTREAMS takes a job name and "
                 "optional cursor and limit arguments"); }
        wireproto::req_message req(proto::LISTSTREAMS::tag,
                                   conn.success()->allocsequencenr());
        req.addparam(proto::LISTSTREAMS::req::job,
                     parsers::_jobname()
                     .match(argv[4])
                     .fatal("parsing job name " + fields::mk(argv[4])));
        if (argc > 5) {
            req.addparam(proto::LISTSTREAMS::req::cursor,
                         parsers::_streamname()
                         .match(argv[5])
                         .fatal("parsing stream name " +fields::mk(argv[5]))); }
        if (argc > 6) {
            req.addparam(proto::LISTSTREAMS::req::limit,
                         parsers::intparser<unsigned>()
                         .match(argv[6])
                         .fatal("parsing limit " + fields::mk(argv[6]))); }
        auto m = conn.success()->call(clientio::CLIENTIO, req)
            .fatal(fields::mk("cannot list streams"));
        fields::print(
            "cursor: " +
            fields::mk(m->getparam(proto::LISTSTREAMS::resp::cursor)) +
            "\n");
        list<streamstatus> streams;
        m->fetch(proto::LISTSTREAMS::resp::streams, streams)
            .fatal("getting streams list");
        fields::print("streams: " + fields::mk(streams) + "\n");
        streams.flush();
    } else if (strcmp(argv[3], "REMOVESTREAM") == 0) {
        if (argc != 6) {
            errx(1, "REMOVESTREAM needs a job and a stream name"); }
        delete conn.success()->call(
            clientio::CLIENTIO,
            wireproto::req_message(proto::REMOVESTREAM::tag,
                                   conn.success()->allocsequencenr())
            .addparam(proto::REMOVESTREAM::req::job,
                      parsers::_jobname()
                      .match(argv[4])
                      .fatal("parsing job name " + fields::mk(argv[4])))
            .addparam(proto::REMOVESTREAM::req::stream,
                      parsers::_streamname()
                      .match(argv[5])
                      .fatal("parsing stream name " + fields::mk(argv[5]))))
            .fatal(fields::mk("cannot remove stream"));
    } else {
        errx(1, "unknown mode %s", argv[3]); }

    conn.success()->destroy(clientio::CLIENTIO);
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging(); }