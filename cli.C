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
#include "storageslave.H"
#include "wireproto.H"

#include "rpcconn.tmpl"
#include "wireproto.tmpl"

#include "fieldfinal.H"

int
main(int argc, const char *const argv[])
{
    if (argc < 3)
        errx(1, "need a socket to connect to and a mode argument");
    const char *sock = argv[1];
    const char *mode = argv[2];
    const char *const *args = argv + 3;
    int nrargs = argc - 3;
    initlogging("cli");
    initpubsub();
    auto c(rpcconn::connect<rpcconn>(
               clientio::CLIENTIO,
               /* Unix domain connections need no authentication */
               rpcconnauth::mkdone(),
               peername::local(sock)));
    int r;

    if (c.isfailure())
        c.failure().fatal("connecting to master");

    r = 0;
    if (!strcmp(mode, "PING")) {
        if (nrargs != 0) errx(1, "PING mode takes no arguments");
        auto m = c.success()->call(
            clientio::CLIENTIO,
            wireproto::req_message(proto::PING::tag,
                                   c.success()->allocsequencenr()).
            addparam(proto::PING::req::msg, "Hello"));
        if (m.issuccess()) {
            fields::print("master sequence " +
                          fields::mk(m.success()->getparam(proto::PING::resp::cntr)) +
                          ", message " +
                          fields::mk(m.success()->getparam(proto::PING::resp::msg)) +
                          "\n");
            delete m.success();
        } else {
            m.failure().fatal("sending ping"); }
    } else if (!strcmp(mode, "LOGS")) {
        if (nrargs) errx(1, "LOGS mode takes no arguments");
        memlog_idx cursor(memlog_idx::min);
        unsigned nr = 200;
        while (1) {
            auto m = c.success()->call(
                clientio::CLIENTIO,
                wireproto::req_message(proto::GETLOGS::tag,
                                       c.success()->allocsequencenr())
                .addparam(proto::GETLOGS::req::startidx, cursor)
                .addparam(proto::GETLOGS::req::nr, nr));
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
            mm->fetch(proto::GETLOGS::resp::msgs, msgs)
                .fatal(fields::mk("decoding returned message list"));
            for (auto it(msgs.start()); !it.finished(); it.next())
                printf("%9ld: %s\n", it->idx.as_long(), it->msg);
            msgs.flush();
            auto s(mm->getparam(proto::GETLOGS::resp::resume));
            delete mm;
            if (!s)
                break; /* we're done */
            cursor = s.just();
        }
    } else if (!strcmp(mode, "STATUS")) {
        using namespace proto::STATUS;
        if (nrargs) errx(1, "STATUS mode takes no arguments");
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
                              fields::mk(mm->getparam(resp::coordinator)) +
                          "\nstorageslave: " +
                              fields::mk(mm->getparam(resp::storageslave)) +
                          "\n");
            delete mm; }
    } else if (!strcmp(mode, "QUIT")) {
        if (nrargs != 2) {
            errx(1, "QUIT mode takes code and explanation arguments"); }
        auto code(shutdowncode::parse(args[0]));
        if (code.isfailure())
            code.failure().fatal(fields::mk("cannot parse shutdown code"));
        const char *message = args[1];
        c.success()->send(
            clientio::CLIENTIO,
            wireproto::tx_message(proto::QUIT::tag)
            .addparam(proto::QUIT::req::message, message)
            .addparam(proto::QUIT::req::reason, code.success()))
            .fatal(fields::mk("sending QUIT message"));
        c.success()->drain(clientio::CLIENTIO);
    } else if (!strcmp(mode, "LISTENING")) {
        using namespace proto::LISTENING;
        if (nrargs) errx(1, "LISTENING mode takes no arguments");
        auto m(c.success()->call(
                   clientio::CLIENTIO,
                   wireproto::req_message(tag,
                                          c.success()->allocsequencenr()))
               .fatal("sending LISTENING message"));
        fields::print(
            "coordinator: " +
                fields::mk(m->getparam(resp::coordinator)) + "\n" +
            "storageslave: " +
                fields::mk(m->getparam(resp::storageslave)) + "\n"
            "control:" +
                fields::mk(m->getparam(resp::control)) + "\n"
            "beacon:" +
                fields::mk(m->getparam(resp::beacon)) + "\n");
        delete m; }
    else {
        printf("Unknown command %s.  Known: PING, LOGS\n", mode);
        r = 1; }
    c.success()->destroy(clientio::CLIENTIO);
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging();
    return r;
}
