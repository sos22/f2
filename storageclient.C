#include <err.h>
#include <stdio.h>
#include <unistd.h>

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

#include "parsers.tmpl"
#include "rpcconn.tmpl"

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
    } else {
        errx(1, "unknown mode %s", argv[3]); }

    conn.success()->destroy(clientio::CLIENTIO);
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging(); }
