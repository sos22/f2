#include "err.h"

#include "beaconclient.H"
#include "coordinator.H"
#include "connpool.H"
#include "jobname.H"
#include "filesystem.H"
#include "logging.H"
#include "nnp.H"
#include "parsers.H"
#include "rpcservice2.H"
#include "serialise.H"
#include "streamname.H"
#include "streamstatus.H"

#include "list.tmpl"
#include "parsers.tmpl"
#include "rpcservice2.tmpl"

class coordinatorservice : public rpcservice2 {
public: filesystem &fs;
public: coordinatorservice(const constoken &token,
                           filesystem &_fs)
    : rpcservice2(token, interfacetype::coordinator),
      fs(_fs) {}
public: orerror<void> called(clientio,
                             deserialise1 &,
                             interfacetype,
                             nnp<incompletecall>,
                             onconnectionthread); };

orerror<void>
coordinatorservice::called(clientio io,
                           deserialise1 &ds,
                           interfacetype t,
                           nnp<incompletecall> ic,
                           onconnectionthread oct) {
    assert(t == interfacetype::coordinator);
    proto::coordinator::tag tag(ds);
    if (tag == proto::coordinator::tag::findjob) {
        jobname jn(ds);
        if (ds.isfailure()) return ds.failure();
        list<slavename> res(fs.findjob(jn));
        ic->complete([capres = res.steal()]
                     (serialise1 &s,
                      mutex_t::token /* txlock */,
                      onconnectionthread) {
                         s.push(capres); },
                     acquirestxlock(io),
                     oct);
        return Success; }
    else if (tag == proto::coordinator::tag::findstream) {
        jobname jn(ds);
        streamname sn(ds);
        if (ds.isfailure()) return ds.failure();
        list<pair<slavename, streamstatus> > res(fs.findstream(jn, sn));
        ic->complete([capres = res.steal()]
                     (serialise1 &s,
                      mutex_t::token /* txlock */,
                      onconnectionthread) {
                         s.push(capres); },
                     acquirestxlock(io),
                     oct);
        return Success; }
    else {
        /* Tag deserialiser shouldn't let us get here. */
        abort(); } }

int
main(int argc, char *argv[]) {
    initlogging("coordinator");
    initpubsub();

    if (argc != 3) {
        errx(1, "need two arguments, the cluster to join and our own name"); }
    auto cluster(parsers::__clustername()
                 .match(argv[1])
                 .fatal("parsing cluser name " + fields::mk(argv[1])));
    auto name(parsers::_slavename()
              .match(argv[2])
              .fatal("parsing slave name " + fields::mk(argv[2])));
    auto bc(beaconclient::build(cluster)
            .fatal("creating beacon client"));
    auto pool(connpool::build(cluster)
              .fatal("creating connection pool"));
    auto &fs(filesystem::build(pool, *bc));

    auto service(rpcservice2::listen<coordinatorservice>(
                     clientio::CLIENTIO,
                     cluster,
                     name,
                     peername::loopback(peername::port::any),
                     fs)
                 .fatal("listening on coordinator interface"));

    while (true) timedelta::hours(1).future().sleep(clientio::CLIENTIO);
    return 0; }
