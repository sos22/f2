/* Simple thing which instantiates a filesystem and then exposes it
 * over an RPC interface. */
#include <err.h>

#include "beaconclient.H"
#include "connpool.H"
#include "filesystem.H"
#include "filesystemproto.H"
#include "jobname.H"
#include "logging.H"
#include "nnp.H"
#include "rpcservice2.H"
#include "serialise.H"
#include "streamname.H"
#include "streamstatus.H"

#include "list.tmpl"
#include "parsers.tmpl"
#include "rpcservice2.tmpl"

class filesystemservice : public rpcservice2 {
public: filesystem &fs;
public: filesystemservice(const constoken &token,
                          filesystem &_fs)
    : rpcservice2(token, interfacetype::filesystem),
      fs(_fs) {}
public: orerror<void> called(clientio,
                             deserialise1 &,
                             interfacetype,
                             nnp<incompletecall>,
                             onconnectionthread); };

orerror<void>
filesystemservice::called(clientio io,
                          deserialise1 &ds,
                          interfacetype t,
                          nnp<incompletecall> ic,
                          onconnectionthread oct) {
    assert(t == interfacetype::filesystem);
    proto::filesystem::tag tag(ds);
    if (tag == proto::filesystem::tag::findjob) {
        jobname jn(ds);
        if (ds.isfailure()) return ds.failure();
        list<agentname> res(fs.findjob(jn));
        ic->complete([capres = res.steal()]
                     (serialise1 &s,
                      mutex_t::token /* txlock */,
                      onconnectionthread) {
                         s.push(capres); },
                     acquirestxlock(io),
                     oct);
        return Success; }
    else if (tag == proto::filesystem::tag::findstream) {
        jobname jn(ds);
        streamname sn(ds);
        if (ds.isfailure()) return ds.failure();
        list<pair<agentname, streamstatus> > res(fs.findstream(jn, sn));
        ic->complete([capres = res.steal()]
                     (serialise1 &s,
                      mutex_t::token /* txlock */,
                      onconnectionthread) {
                         s.push(capres); },
                     acquirestxlock(io),
                     oct);
        return Success; }
    else if (tag == proto::filesystem::tag::nominateagent) {
        maybe<jobname> jn(ds);
        if (ds.isfailure()) return ds.failure();
        ic->complete([res = fs.nominateagent(jn)]
                     (serialise1 &s,
                      mutex_t::token /* txlock */,
                      onconnectionthread) {
                         s.push(res); },
                     acquirestxlock(io),
                     oct);
        return Success; }
    else if (tag == proto::filesystem::tag::storagebarrier) {
        agentname an(ds);
        proto::eq::eventid eid(ds);
        if (ds.isfailure()) return ds.failure();
        /* XXX we're not watching for aborts here! */
        fs.storagebarrier(
            an,
            eid,
            [ic] (clientio _io) {
                ic->complete(
                    [] (serialise1 &, mutex_t::token /* txlock */) {},
                    _io); });
        return Success; }
    else {
        /* Tag deserialiser shouldn't let us get here. */
        abort(); } }

int
main(int argc, char *argv[]) {
    initlogging("filesystem");
    initpubsub();
    
    if (argc != 3) {
        errx(1, "need two arguments, the cluster to join and our own name"); }
    auto cluster(parsers::__clustername()
                 .match(argv[1])
                 .fatal("parsing cluster name " + fields::mk(argv[1])));
    auto name(parsers::_agentname()
              .match(argv[2])
              .fatal("parsing agent name " + fields::mk(argv[2])));
    auto bc(beaconclient::build(cluster)
            .fatal("creating beacon client"));
    auto pool(connpool::build(cluster)
              .fatal("creating connection pool"));
    auto &fs(filesystem::build(pool, *bc));
    
    auto service(rpcservice2::listen<filesystemservice>(
                     clientio::CLIENTIO,
                     cluster,
                     name,
                     peername::all(peername::port::any),
                     fs)
                 .fatal("listening on filesystem interface"));
    
    while (true) timedelta::hours(1).future().sleep(clientio::CLIENTIO);
    return 0; }
