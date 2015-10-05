/* Simple thing which watches for changes on a storage agent. */
#include <err.h>

#include "clientio.H"
#include "connpool.H"
#include "eqclient.H"
#include "fields.H"
#include "logging.H"
#include "main.H"
#include "parsers.H"
#include "pubsub.H"
#include "storage.H"

#include "parsers.tmpl"

orerror<void>
f2main(list<string> &args) {
    initlogging("storagespy");
    initpubsub();
    if (args.length() != 2) {
        errx(1, "need at two arguments: a cluster and a peer"); }
    auto cluster(parsers::__clustername()
                 .match(args.idx(0))
                 .fatal("parsing cluser name " + fields::mk(args.idx(0))));
    auto peer(parsers::_agentname()
              .match(args.idx(1))
              .fatal("parsing agent name " + fields::mk(args.idx(1))));
    auto &pool(*connpool::build(cluster).fatal("building conn pool"));

    auto clnt(eqclient<proto::storage::event>::connect(
                  clientio::CLIENTIO,
                  pool,
                  peer,
                  proto::eq::names::storage,
                  timedelta::seconds(30).future())
              .fatal("connecting storage event queue")
              .first());
    while (true) {
        auto p(clnt->pop(clientio::CLIENTIO)
               .fatal("getting event from storage agent"));
        logmsg(loglevel::info, p.field()); } }
