#ifndef STORAGESLAVE_H__
#define STORAGESLAVE_H__

#include "controlserver.H"
#include "rpcconn.H"

class clientio;
class controlserver;
template <typename> class orerror;
class registrationsecret;

class storageslavestatus {
    /* For now, the only status exposed by the storage slave is for
       its connection to the master. */
private: rpcconn::status_t unwrap() const { return conn; }
public:  rpcconn::status_t conn;
public:  storageslavestatus(const rpcconn::status_t &_conn)
    : conn(_conn) {}
WIREPROTO_WRAPPER_TYPE(storageslavestatus, rpcconn::status_t);
};

namespace fields {
class field;
const field &mk(const storageslavestatus &);
}

class storageslave : public rpcconn {
    friend class rpcconn;
private: class statusiface : public statusinterface {
    private: storageslave *const owner;
    public:  statusiface(storageslave *_owner,
                         controlserver *cs)
        : statusinterface(cs),
          owner(_owner) {}
    private: void getstatus(wireproto::tx_message *msg) const;
    };
private: statusiface status_;
private: storageslave(socket_t &_socket,
                      const peername &_peer,
                      controlserver *cs)
    : rpcconn(_socket, _peer),
      status_(this, cs) {}
public:  static orerror<storageslave *> build(
    clientio io,
    const registrationsecret &rs,
    controlserver *);
public:  void destroy(clientio);

public:  typedef storageslavestatus status_t;
public:  status_t status() const;
};

namespace fields {
const field &mk(const storageslave::status_t &);
}

#endif /* !STORAGESLAVE_H__ */
