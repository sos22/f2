#include "agentname.H"
#include "clientio.H"
#include "clustername.H"
#include "connpool.H"
#include "filesystemagent.H"
#include "filesystemclient.H"
#include "peername.H"
#include "rpcservice2.H"
#include "storageagent.H"
#include "storageclient.H"
#include "storageconfig.H"
#include "test2.H"

#include "orerror.tmpl"
#include "test2.tmpl"

class teststorage {
public:  const filename dir;
private: const orerror<void> fmtres;
public:  const agentname an;
public:  ::storageagent &storageagent;
public:  ::storageclient &storageclient;
public: teststorage(
    clientio io,
    quickcheck &q,
    const clustername &cluster,
    const agentname &_an,
    connpool &cp)
    : dir(q),
      fmtres(::storageagent::format(dir)),
      an(_an),
      storageagent(*::storageagent::build(io, cluster, an, dir)
                   .fatal("starting storage agent")),
      storageclient(::storageclient::connect(cp, an)) {
    fmtres.fatal("formatting storage pool"); }
public: ~teststorage() {
    storageclient.destroy();
    storageagent.destroy(clientio::CLIENTIO);
    dir.rmtree().fatal("rmtree storage area " + dir.field()); } };

static testmodule __testfilesystemagent(
    "filesystemagent",
    list<filename>::mk("filesystemagent.C",
                       "filesystemagent.H",
                       "filesystemclient.C",
                       "filesystemclient.H",
                       "filesystemproto.C",
                       "filesystemproto.H"),
    testmodule::LineCoverage(70_pc),
    testmodule::BranchCoverage(50_pc),
    "basic", [] (clientio io) {
        quickcheck q;
        auto cluster(mkrandom<clustername>(q));
        agentname fsagentname("fsagent");
        auto &fsagent(*filesystemagent(
                          io,
                          cluster,
                          fsagentname,
                          peername::all(peername::port::any))
                      .fatal("starting filesystem agent"));
        auto &cp(*connpool::build(cluster).fatal("building connpool"));
        teststorage sa1(io, q, cluster, agentname("storageagent"), cp);
        teststorage sa2(io, q, cluster, agentname("storageagent2"), cp);
        job j(filename("library"),
              "function",
              empty,
              empty);
        auto &fsclient(filesystemclient::connect(cp, fsagentname));
        assert(fsclient
               .findjob(io, j.name())
               .fatal("querying non-existent job")
               .empty());
        auto evt(sa1.storageclient.createjob(io, j).fatal("creating job"));
        fsclient.storagebarrier(io, sa1.an, evt).fatal("storage barrier");
        auto agentlist(fsclient
                       .findjob(io, j.name())
                       .fatal("querying filesystem"));
        assert(agentlist.length() == 1);
        assert(agentlist.idx(0) == sa1.an);
        auto evt2(sa2.storageclient.createjob(io, j).fatal("creating job2"));
        fsclient.storagebarrier(io, sa2.an, evt2).fatal("storage barrier2");
        agentlist = fsclient
            .findjob(io, j.name())
            .fatal("re-querying filesystem");
        assert(agentlist.length() == 2);
        assert(agentlist.idx(0) == sa1.an || agentlist.idx(1) == sa1.an);
        assert(agentlist.idx(0) == sa2.an || agentlist.idx(1) == sa2.an);
        assert(agentlist.idx(0) != agentlist.idx(1));
        fsclient.destroy();
        fsagent.destroy(io);
        cp.destroy(); },
    "barrier", [] (clientio io) {
        quickcheck q;
        auto cluster(mkrandom<clustername>(q));
        agentname fsagentname("fsagent");
        auto &fsagent(*filesystemagent(
                          io,
                          cluster,
                          fsagentname,
                          peername::all(peername::port::any))
                      .fatal("starting filesystem agent"));
        auto &cp(*connpool::build(cluster).fatal("building connpool"));
        auto &fsclient(filesystemclient::connect(cp, fsagentname));
        teststorage sa1(io, q, cluster, agentname("storageagent"), cp);
        bool havemissing = false;
        bool havepresent = false;
        int mat;
        int pat;
        int x;
        for (x = 0; !havemissing || !havepresent; x++) {
            job j(filename("library"),
                  ("function" + fields::mk(x)).c_str(),
                  empty,
                  empty);
            auto evt(sa1.storageclient.createjob(io, j).fatal("creating job"));
            if (fsclient
                .findjob(io, j.name())
                .fatal("querying job")
                .empty()) {
                if (!havemissing) mat = x;
                havemissing = true; }
            else {
                if (!havepresent) pat = x;
                havepresent = true; }
            fsclient.storagebarrier(io, sa1.an, evt).fatal("storage barrier");
            assert(!fsclient
                   .findjob(io, j.name())
                   .fatal("querying job")
                   .empty()); }
        logmsg(loglevel::info,
               "missing at " + fields::mk(mat) + ", "
               "present at " + fields::mk(pat));
        fsclient.destroy();
        fsagent.destroy(io);
        cp.destroy(); },
    "dropjob", [] (clientio io) {
        quickcheck q;
        auto cluster(mkrandom<clustername>(q));
        agentname fsagentname("fsagent");
        auto &fsagent(*filesystemagent(
                          io,
                          cluster,
                          fsagentname,
                          peername::all(peername::port::any))
                      .fatal("starting filesystem agent"));
        auto &cp(*connpool::build(cluster).fatal("building connpool"));
        auto &fsclient(filesystemclient::connect(cp, fsagentname));
        teststorage sa(io, q, cluster, agentname("storageagent"), cp);
        job j("library", "function", empty, empty);
        {   auto evt(sa.storageclient.createjob(io, j).fatal("creating job"));
            fsclient.storagebarrier(io, sa.an, evt).fatal("barrier1"); }
        assert(!fsclient
               .findjob(io, j.name())
               .fatal("querying job")
               .empty());
        {   auto evt = sa.storageclient.removejob(io, j.name())
                .fatal("removing job");
            fsclient.storagebarrier(io, sa.an, evt).fatal("barrier2"); }
        assert(fsclient
               .findjob(io, j.name())
               .fatal("querying job")
               .empty());
        fsclient.destroy();
        fsagent.destroy(io);
        cp.destroy(); },
    "findstream", [] (clientio io) {
        quickcheck q;
        auto cluster(mkrandom<clustername>(q));
        agentname fsagentname("fsagent");
        auto &fsagent(*filesystemagent(
                          io,
                          cluster,
                          fsagentname,
                          peername::all(peername::port::any))
                      .fatal("starting filesystem agent"));
        auto &cp(*connpool::build(cluster).fatal("building connpool"));
        auto &fsclient(filesystemclient::connect(cp, fsagentname));
        teststorage sa(io, q, cluster, agentname("storageagent"), cp);
        auto sn(streamname::mk("output").fatal("make output streamname"));
        job j("library",
              "function",
              empty,
              list<streamname>(Immediate(), sn));
        {   auto n(fsclient
                   .findstream(io, j.name(), sn)
                   .fatal("findstream on non-existent job"));
            assert(n.empty()); }
        {   auto evt(sa.storageclient.createjob(io, j).fatal("creating job"));
            fsclient.storagebarrier(io, sa.an, evt).fatal("barrier1"); }
        {   auto n(fsclient
                   .findstream(io, j.name(), sn)
                   .fatal("findstream on non-existent job"));
            assert(!n.empty());
            assert(n.length() == 1);
            assert(n.peekhead().first() == sa.an);
            assert(!n.peekhead().second().isfinished());
            assert(n.peekhead().second().isempty()); }
        {   auto evt(sa.storageclient.finish(io, j.name(), sn).fatal("finish"));
            fsclient.storagebarrier(io, sa.an, evt).fatal("barrier2"); }
        {   auto n(fsclient
                   .findstream(io, j.name(), sn)
                   .fatal("findstream on non-existent job"));
            assert(!n.empty());
            assert(n.length() == 1);
            assert(n.peekhead().first() == sa.an);
            assert(n.peekhead().second().isfinished());
            assert(n.peekhead().second().isempty()); }
        fsclient.destroy();
        fsagent.destroy(io);
        cp.destroy(); },
    "clientname", [] (clientio) {
        quickcheck q;
        auto cluster(mkrandom<clustername>(q));
        agentname fsagentname(q);
        auto &cp(*connpool::build(cluster).fatal("building connpool"));
        auto &fsc(filesystemclient::connect(cp, fsagentname));
        assert(fsc.name() == fsagentname);
        fsc.destroy();
        cp.destroy(); } );
