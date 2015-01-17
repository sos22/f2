/* Simple thing which watches for changes on a storage slave. */
#include <err.h>

#include "clientio.H"
#include "connpool.H"
#include "eqclient.H"
#include "fields.H"
#include "logging.H"
#include "parsers.H"
#include "pubsub.H"
#include "storage.H"

#include "parsers.tmpl"

int
main(int argc, char *argv[]) {
    initlogging("storagespy");
    initpubsub();
    if (argc != 3) errx(1, "need at two arguments: a cluster and a peer");
    auto cluster(parsers::__clustername()
                 .match(argv[1])
                 .fatal("parsing cluser name " + fields::mk(argv[1])));
    auto peer(parsers::_slavename()
              .match(argv[2])
              .fatal("parsing slave name " + fields::mk(argv[2])));
    auto &pool(*connpool::build(cluster).fatal("building conn pool"));

    auto clnt(eqclient<proto::storage::event>::connect(
                  clientio::CLIENTIO,
                  pool,
                  peer,
                  proto::eq::names::storage,
                  timedelta::seconds(30).future())
              .fatal("connecting storage event queue"));
    while (true) {
        auto p(clnt->pop(clientio::CLIENTIO)
               .fatal("getting event from storage slave"));
        logmsg(loglevel::info, p.field()); } }
