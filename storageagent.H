#ifndef STORAGEAGENT_H__
#define STORAGEAGENT_H__

#include "eq.H"
#include "rpcservice2.H"
#include "storageconfig.H"

class bytecount;
class clientio;
class eqserver;
template <typename> class eventqueue;
template <typename> class orerror;
class serialise1;
class streamname;
class streamstatus;
class job;
class jobname;

namespace proto {
namespace storage {
class event; } }

class storageagent : public rpcservice2 {
    friend class rpcservice2;

private: const storageconfig config;
private: mutex_t mux;
private: eqserver &eqs;
private: eventqueue<proto::storage::event> &eqq;
public:  static orerror<void> format(const filename &fn);
public:  static orerror<nnp<storageagent> > build(clientio,
                                                  const storageconfig &config);
public:  static orerror<nnp<storageagent> > build(clientio,
                                                  const clustername &,
                                                  const agentname &,
                                                  const filename &);
private: storageagent(const constoken &,
                      const storageconfig &config,
                      eqserver &eqs,
                      eventqueue<proto::storage::event> &eqq);
private: orerror<void> initialise(clientio) final;
private: void destroying(clientio io);
    /* Use destroy() instead */
private: ~storageagent();
private: orerror<void> called(
    clientio,
    deserialise1 &,
    interfacetype,
    nnp<incompletecall>,
    onconnectionthread) final;
private: orerror<void> _called(
    clientio,
    deserialise1 &,
    interfacetype,
    nnp<incompletecall>,
    onconnectionthread,
    mutex_t::token);
private: orerror<proto::eq::eventid> createjob(
    clientio io,
    const job &t);
public:  orerror<void> append(
    const jobname &t,
    const streamname &sn,
    bytecount oldsize,
    buffer &b);
public:  orerror<streamstatus> finish(
    const jobname &t,
    const streamname &sn);
private: orerror<void> read(
    const jobname &jn,
    const streamname &sn,
    bytecount start,
    bytecount end,
    nnp<incompletecall> ic,
    acquirestxlock atl,
    onconnectionthread oct) const;
private: orerror<void> listjobs(
    nnp<incompletecall> ic,
    acquirestxlock atl,
    onconnectionthread oct) const;
private: orerror<void> statjob(
    const jobname &j,
    nnp<incompletecall> ic,
    acquirestxlock atl,
    onconnectionthread oct) const;
private: orerror<void> liststreams(
    const jobname &jn,
    nnp<incompletecall> ic,
    acquirestxlock atl,
    onconnectionthread oct) const;
private: orerror<streamstatus> statstream(
    const jobname &jn,
    const streamname &sn);
public:  orerror<void> removejob(
    const jobname &jn); };

#endif /* !STORAGEAGENT_H__ */
