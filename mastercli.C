#include <err.h>
#include <stdio.h>
#include <string.h>

#include "beaconserver.H"
#include "coordinator.H"
#include "fields.H"
#include "logging.H"
#include "peername.H"
#include "proto.H"
#include "pubsub.H"
#include "registrationsecret.H"
#include "rpcconn.H"
#include "shutdown.H"
#include "wireproto.H"

#include "fieldfinal.H"

int
main(int argc, char *argv[])
{
    if (argc < 2)
        errx(1, "need a mode argument");
    initlogging("mastercli");
    initpubsub();
    auto c(rpcconn::connect<rpcconn>(
               clientio::CLIENTIO, peername::local("mastersock")));
    int r;

    if (c.isfailure())
        c.failure().fatal("connecting to master");

    r = 0;
    if (!strcmp(argv[1], "PING")) {
        if (argc != 2) errx(1, "PING mode takes no arguments");
        auto snr(c.success()->allocsequencenr());
        auto m = c.success()->call(
            clientio::CLIENTIO,
            wireproto::req_message(proto::PING::tag, snr).
            addparam(proto::PING::req::msg, "Hello"));
        c.success()->putsequencenr(snr);
        if (m.issuccess()) {
            fields::print("master ping sequence " +
                          fields::mk(m.success()->getparam(proto::PING::resp::cntr)) +
                          ", message " +
                          fields::mk(m.success()->getparam(proto::PING::resp::msg)) +
                          "\n");
            delete m.success();
        } else {
            m.failure().fatal("sending ping"); }
    } else if (!strcmp(argv[1], "LOGS")) {
        if (argc != 2) errx(1, "LOGS mode takes no arguments");
        memlog_idx cursor(memlog_idx::min);
        unsigned nr = 200;
        while (1) {
            auto snr(c.success()->allocsequencenr());
            auto m = c.success()->call(
                clientio::CLIENTIO,
                wireproto::req_message(proto::GETLOGS::tag, snr)
                .addparam(proto::GETLOGS::req::startidx, cursor)
                .addparam(proto::GETLOGS::req::nr, nr));
            c.success()->putsequencenr(snr);
            if (m.isfailure()) {
                if (m.failure() == error::overflowed) {
                    if (nr == 1) {
                        m.failure().fatal(
                            "overflowed with a single log message?"); }
                    nr /= 2;
                    continue; }
                m.failure().fatal("requesting logs"); }
            auto mm(m.success());
            list<memlog_entry> msgs;
            auto rr(mm->fetch(proto::GETLOGS::resp::msgs, msgs));
            if (rr.isjust())
                rr.just().fatal(fields::mk("decoding returned message list"));
            for (auto it(msgs.start()); !it.finished(); it.next())
                printf("%9ld: %s\n", it->idx.as_long(), it->msg);
            msgs.flush();
            auto s(mm->getparam(proto::GETLOGS::resp::resume));
            delete mm;
            if (!s)
                break; /* we're done */
            cursor = s.just();
        }
    } else if (!strcmp(argv[1], "STATUS")) {
        using namespace proto::STATUS;
        if (argc != 2) errx(1, "STATUS mode takes no arguments");
        auto snr(c.success()->allocsequencenr());
        auto m = c.success()->call(
            clientio::CLIENTIO,
            wireproto::req_message(tag, snr));
        if (m.isfailure()) {
            fields::print(fields::mk(m.failure()));
        } else {
            auto mm(m.success());
            fields::print("beacon: " + fields::mk(mm->getparam(resp::beacon)) +
                          "\ncoordinator: " +
                              fields::mk(mm->getparam(resp::beacon)) +
                          "\n");
            delete mm; }
    } else if (!strcmp(argv[1], "QUIT")) {
        if (argc != 4)
            errx(1, "QUIT mode takes code and explanation arguments");
        auto code(shutdowncode::parse(argv[2]));
        if (code.isfailure())
            code.failure().fatal(fields::mk("cannot parse shutdown code"));
        const char *message = argv[3];
        auto rv(c.success()->send(
                    clientio::CLIENTIO,
                    wireproto::tx_message(proto::QUIT::tag)
                    .addparam(proto::QUIT::req::message, message)
                    .addparam(proto::QUIT::req::reason, code.success())));
        if (rv.isjust())
            rv.just().fatal(fields::mk("sending QUIT message"));
        c.success()->drain(clientio::CLIENTIO);
    } else {
        printf("Unknown command %s.  Known: PING, LOGS\n", argv[1]);
        r = 1;
    }
    c.success()->destroy(clientio::CLIENTIO);
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging();
    return r;
}
