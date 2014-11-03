#ifndef STORAGESLAVE_H__
#define STORAGESLAVE_H__

#include "rpcservice.H"
#include "storageconfig.H"

class clientio;
template <typename> class orerror;
class streamname;
class jobname;

class storageslave : public rpcservice {
    friend class rpcservice;

private: const storageconfig config;
    /* mutable because we acquire it from the const status() method. */
private: mutable mutex_t mux;
private: beaconserver *beacon;
public:  static orerror<storageslave *> build(clientio,
                                              const storageconfig &config);
private: storageslave(const constoken &,
                      const storageconfig &config);
private: orerror<void> initialise(clientio) final;
private: void call(const wireproto::rx_message &, response *) final;
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
private: void read(
    response *resp,
    const jobname &jn,
    const streamname &sn,
    uint64_t start,
    uint64_t end) const;
private: void listjobs(
    response *resp,
    const maybe<jobname> &cursor,
    const maybe<unsigned> &limit) const;
private: void liststreams(
    response *resp,
    const jobname &jn,
    const maybe<streamname> &cursor,
    const maybe<unsigned> &limit) const;
public:  orerror<void> removestream(
    const jobname &jn,
    const streamname &sn);
private: void destroying(clientio) final; };

#endif /* !STORAGESLAVE_H__ */
