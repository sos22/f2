/* Things to set up contexts common to multiple test modules. */
#ifndef TESTCTXT_H__
#define TESTCTXT_H__

#include "computeagent.H"
#include "computeclient.H"
#include "filesystemagent.H"
#include "filesystemclient.H"
#include "storageagent.H"
#include "storageclient.H"

class computetest {
public:  quickcheck q;
public:  const clustername cluster;
public:  const agentname fsagentname;
public:  const agentname computeagentname;
public:  const agentname storageagentname;
public:  const filename computedir;
private: const orerror<void> computeformatres;
public:  const filename storagedir;
private: const orerror<void> storageformatres;
public:  connpool &cp;
public:  ::storageagent &storageagent;
public:  rpcservice2 &filesystemagent;
public:  ::computeagent *computeagent;
public:  storageclient &sc;
public:  filesystemclient &fsc;
public:  computeclient &cc;
public:  explicit computetest(clientio io)
    : q(),
      cluster(mkrandom<clustername>(q)),
      fsagentname("fsagent"),
      computeagentname("computeagent"),
      storageagentname("storageagent"),
      computedir(q),
      computeformatres(::computeagent::format(computedir).warn("computedir")),
      storagedir(q),
      storageformatres(::storageagent::format(storagedir).warn("storagedir")),
      cp(*connpool::build(cluster).fatal("building connpool")),
      storageagent(*::storageagent::build(
                       io,
                       cluster,
                       storageagentname,
                       storagedir)
                   .fatal("starting storage agent")),
      filesystemagent(*::filesystemagent(
                          io,
                          cluster,
                          fsagentname,
                          peername::all(peername::port::any))
                      .fatal("starting filesystem agent")),
      computeagent(computeagent::build(io,
                                       cluster,
                                       fsagentname,
                                       computeagentname,
                                       computedir)
                   .fatal("starting compute agent")),
      sc(storageclient::connect(cp, storageagentname)),
      fsc(filesystemclient::connect(cp, fsagentname)),
      cc(computeclient::connect(cp, computeagentname)) {
    computeformatres.fatal("formatting compute agent dir"); }
    /* Create a job on the storage agent and wait for the filesystem
     * to pick it up. */
public:  void createjob(clientio io, const job &j) {
    auto createevt(sc.createjob(io, j).fatal("creating job"));
    fsc.storagebarrier(io, storageagentname, createevt)
        .fatal("awaiting job creation"); }
    /* Fill a job's output with a particular string and then finish
     * it. */
public:  void filloutput(clientio io,
                         jobname jn,
                         const streamname &sn,
                         const char *content) {
    buffer ib;
    ib.queue(content, strlen(content));
    sc.append(io, jn, sn, ib, 0_B).fatal("filling output");
    sc.finish(io, jn, sn).fatal("finishing output"); }
public:  ~computetest() {
    cc.destroy();
    fsc.destroy();
    sc.destroy();
    if (computeagent != NULL) computeagent->destroy(clientio::CLIENTIO);
    filesystemagent.destroy(clientio::CLIENTIO);
    storageagent.destroy(clientio::CLIENTIO);
    cp.destroy();
    computedir.rmtree().fatal("removing compute dir"); } };

#endif /* !TESTCTXT_H__ */
