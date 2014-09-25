#ifndef STORAGESLAVE_H__
#define STORAGESLAVE_H__

#include "controlserver.H"
#include "filename.H"
#include "rpcconn.H"
#include "rpcserver.H"
#include "storageconfig.H"

class clientio;
class controlserver;
template <typename> class orerror;
class registrationsecret;
class storageslave;
class streamname;
class jobname;

class storageslavestatus {
public:  rpcserver::status_t server;
public:  list<rpcconn::status_t> clientconns;
public:  storageslavestatus(const rpcserver::status_t &_server,
                            const list<rpcconn::status_t> &_clientconns)
    : server(_server),
      clientconns(_clientconns) {}
WIREPROTO_TYPE(storageslavestatus);
};

namespace fields {
class field;
const field &mk(const storageslavestatus &);
}

class storageslaveconn;

class storageslave : rpcserver {
    friend class storageslaveconn;
    friend class pausedthread<storageslave>;
    friend class thread;
private: class controliface : public controlinterface {
    private: storageslave *const owner;
    public:  controliface(storageslave *_owner,
                         controlserver *cs)
        : controlinterface(cs),
          owner(_owner) {}
    private: void getstatus(wireproto::tx_message *msg) const;
    private: void getlistening(wireproto::resp_message *) const;
    };
private: controliface control_;
    /* XXX only used for status interface; maybe move all of that to
     * rpcserver layer? */
private: list<storageslaveconn *> clients;
private: const storageconfig config;
    /* mutable because we acquire it from the const status() method. */
private: mutable mutex_t mux;
public:  static orerror<storageslave *> build(const storageconfig &config,
                                              controlserver *);
private: storageslave(constoken,
                      listenfd,
                      controlserver *cs,
                      const storageconfig &config);
private: orerror<rpcconn *> accept(socket_t);
public:  orerror<void> createempty(
    const jobname &t,
    const streamname &sn) const;
public:  orerror<void> append(
    const jobname &t,
    const streamname &sn,
    buffer &b) const;
public:  orerror<void> finish(
    const jobname &t,
    const streamname &sn) const;
private: rpcconn::messageresult read(
    const wireproto::rx_message &rxm,
    const jobname &jn,
    const streamname &sn,
    uint64_t start,
    uint64_t end) const;
private: rpcconn::messageresult listjobs(
    const wireproto::rx_message &rxm,
    const maybe<jobname> &cursor,
    const maybe<unsigned> &limit) const;
private: rpcconn::messageresult liststreams(
    const wireproto::rx_message &rxm,
    const jobname &jn,
    const maybe<streamname> &cursor,
    const maybe<unsigned> &limit) const;
public:  orerror<void> removestream(
    const jobname &jn,
    const streamname &sn) const;
public:  void destroy(clientio);

public:  typedef storageslavestatus status_t;
public:  status_t status() const;
};

namespace fields {
const field &mk(const storageslave::status_t &);
}

#endif /* !STORAGESLAVE_H__ */
