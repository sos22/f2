#ifndef STORAGESLAVE_H__
#define STORAGESLAVE_H__

#include "controlserver.H"
#include "filename.H"
#include "rpcconn.H"

class clientio;
class controlserver;
template <typename> class orerror;
class registrationsecret;
class storageslave;
class streamname;
class jobname;

class storageslavestatus {
public:  rpcconn::status_t masterconn;
public:  list<rpcconn::status_t> clientconns;
public:  storageslavestatus(const rpcconn::status_t &_masterconn,
                            const list<rpcconn::status_t> &_clientconns)
    : masterconn(_masterconn),
      clientconns(_clientconns) {}
public:  ~storageslavestatus() { clientconns.flush(); }
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
private: class statusiface : public statusinterface {
    private: storageslave *const owner;
    public:  statusiface(storageslave *_owner,
                         controlserver *cs)
        : statusinterface(cs),
          owner(_owner) {}
    private: void getstatus(wireproto::tx_message *msg) const;
    };
private: statusiface status_;
private: registrationsecret const rs;
private: storageslaveconn *masterconn;
private: list<storageslaveconn *> clients;
private: filename pool;
    /* mutable because we acquire it from the const status() method. */
private: mutable mutex_t mux;
public:  static orerror<storageslave *> build(
    clientio io,
    const registrationsecret &rs,
    const filename &,
    controlserver *);
private: storageslave(constoken,
                      listenfd,
                      const registrationsecret &_rs,
                      const filename &,
                      controlserver *cs);
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
private: messageresult read(
    const wireproto::rx_message &rxm,
    const jobname &jn,
    const streamname &sn,
    uint64_t start,
    uint64_t end) const;
private: messageresult listjobs(
    const wireproto::rx_message &rxm,
    const maybe<jobname> &cursor,
    const maybe<unsigned> &limit) const;
private: messageresult liststreams(
    const wireproto::rx_message &rxm,
    const jobname &jn,
    const maybe<streamname> &cursor,
    const maybe<unsigned> &limit) const;
public:  orerror<void> removestream(
    const jobname &jn,
    const streamname &sn) const;
public:  void destroy(clientio);
private: ~storageslave();

public:  typedef storageslavestatus status_t;
public:  status_t status() const;
};

namespace fields {
const field &mk(const storageslave::status_t &);
}

#endif /* !STORAGESLAVE_H__ */
