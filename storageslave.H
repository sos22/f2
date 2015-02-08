#ifndef STORAGESLAVE_H__
#define STORAGESLAVE_H__

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
class jobname;

namespace proto {
namespace storage {
class event; } }

class storageslave : public rpcservice2 {
    friend class rpcservice2;

private: const storageconfig config;
    /* mutable because we acquire it from the const status() method. */
private: mutable mutex_t mux;
private: eqserver &eqs;
private: eventqueue<proto::storage::event> &eqq;
public:  static orerror<void> format(const filename &fn);
public:  static orerror<nnp<storageslave> > build(clientio,
                                                  const storageconfig &config);
private: storageslave(const constoken &,
                      const storageconfig &config,
                      eqserver &eqs,
                      eventqueue<proto::storage::event> &eqq);
public:  void destroy(clientio io);
    /* Use destroy() instead */
private: ~storageslave();
private: orerror<void> called(
    clientio,
    deserialise1 &,
    interfacetype,
    nnp<incompletecall>,
    onconnectionthread) final;
public:  orerror<void> createjob(
    const jobname &t);
public:  orerror<void> createstream(
    const jobname &t,
    const streamname &sn);
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
    const maybe<jobname> &cursor,
    const maybe<unsigned> &limit,
    nnp<incompletecall> ic,
    acquirestxlock atl,
    onconnectionthread oct) const;
private: orerror<void> liststreams(
    const jobname &jn,
    const maybe<streamname> &cursor,
    const maybe<unsigned> &limit,
    nnp<incompletecall> ic,
    acquirestxlock atl,
    onconnectionthread oct) const;
private: orerror<streamstatus> statstream(
    const jobname &jn,
    const streamname &sn);
public:  orerror<void> removestream(
    const jobname &jn,
    const streamname &sn);
public:  orerror<void> removejob(
    const jobname &jn); };

#endif /* !STORAGESLAVE_H__ */
