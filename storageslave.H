#ifndef STORAGESLAVE_H__
#define STORAGESLAVE_H__

#include "rpcservice2.H"
#include "storageconfig.H"

class clientio;
template <typename> class orerror;
class serialise1;
class streamname;
class jobname;

class storageslave : public rpcservice2 {
    friend class rpcservice2;

private: const storageconfig config;
    /* mutable because we acquire it from the const status() method. */
private: mutable mutex_t mux;
private: beaconserver *beacon;
public:  static orerror<nnp<storageslave> > build(clientio,
                                                  const storageconfig &config);
private: storageslave(const constoken &,
                      const storageconfig &config);
private: orerror<void> called(
    clientio,
    onconnectionthread,
    deserialise1 &,
    interfacetype,
    nnp<incompletecall>) final;
private: orerror<void> initialise(clientio) final;
public:  orerror<void> createempty(
    const jobname &t,
    const streamname &sn);
public:  orerror<void> append(
    const jobname &t,
    const streamname &sn,
    buffer &b);
public:  orerror<void> finish(
    const jobname &t,
    const streamname &sn);
private: orerror<void> read(
    const jobname &jn,
    const streamname &sn,
    uint64_t start,
    uint64_t end,
    nnp<incompletecall> ic,
    onconnectionthread oct) const;
private: orerror<void> listjobs(
    const maybe<jobname> &cursor,
    const maybe<unsigned> &limit,
    nnp<incompletecall> ic,
    onconnectionthread oct) const;
private: orerror<void> liststreams(
    const jobname &jn,
    const maybe<streamname> &cursor,
    const maybe<unsigned> &limit,
    nnp<incompletecall> ic,
    onconnectionthread oct) const;
public:  orerror<void> removestream(
    const jobname &jn,
    const streamname &sn);

private: void destroying(clientio) final; };

#endif /* !STORAGESLAVE_H__ */
