#include <err.h>
#include <stdio.h>
#include <unistd.h>

#include "clientio.H"
#include "fields.H"
#include "logging.H"
#include "peername.H"
#include "pubsub.H"
#include "registrationsecret.H"
#include "rpcconn.H"

#include "rpcconn.tmpl"

int
main(int argc, char *argv[]) {
    initlogging("storageclient");
    initpubsub();
    if (argc != 3) errx(1, "need two arguments, a secret and a peer");
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
    /* Just hang around for a bit so that we can see if PING is
     * working. */
    sleep(10);
    printf("shutdown\n");
    conn.success()->destroy(clientio::CLIENTIO);
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging(); }
