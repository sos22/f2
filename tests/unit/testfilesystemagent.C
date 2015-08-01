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

#include "test2.tmpl"

static testmodule __testfilesystemagent(
    "filesystemagent",
    list<filename>::mk("filesystemagent.C",
                       "filesystemagent.H",
                       "filesystemclient.C",
                       "filesystemclient.H",
                       "filesystemproto.C",
                       "filesystemproto.H"),
    testmodule::LineCoverage(45_pc),
    testmodule::BranchCoverage(33_pc),
    "basic", [] (clientio io) {
        quickcheck q;
        clustername cluster(q);
        agentname fsagentname("fsagent");
        auto &fsagent(*filesystemagent(
                          io,
                          cluster,
                          fsagentname,
                          peername::all(peername::port::any))
                      .fatal("starting filesystem agent"));
        filename storagedir(q);
        storageagent::format(storagedir).fatal("formatting storage pool");
        agentname storagename("storageagent");
        auto &storageagent(
            *storageagent::build(
                io,
                storageconfig(
                    storagedir,
                    beaconserverconfig::dflt(cluster, storagename)))
            .fatal("building storageagent"));
        auto &cp(*connpool::build(cluster).fatal("building connpool"));
        job j(filename("library"),
              "function",
              empty,
              empty);
        auto &fsclient(
            *filesystemclient::connect(io, cp, fsagentname)
            .fatal("connecting to filesystem agent"));
        assert(fsclient
               .findjob(io, j.name())
               .fatal("querying non-existent job")
               .empty());
        auto &storageclient(
            *storageclient::connect(io, cp, storagename)
            .fatal("connecting storage client"));
        auto evt(storageclient.createjob(io, j).fatal("creating job"));
        fsclient.storagebarrier(io, storagename, evt).fatal("storage barrier");
        auto agentlist(fsclient
                       .findjob(io, j.name())
                       .fatal("querying filesystem"));
        assert(agentlist.length() == 1);
        assert(agentlist.idx(0) == storagename);
        fsclient.destroy();
        storageclient.destroy();
        fsagent.destroy(io);
        storageagent.destroy(io);
        cp.destroy(); });
