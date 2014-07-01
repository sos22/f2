/* The coordinator is responsible for keeping track of what workers we
   currently have available in the pool and for managing coordination
   with them. */
#ifndef COORDINATOR_H__
#define COORDINATOR_H__

#include "controlserver.H"
#include "mastersecret.H"
#include "registrationsecret.H"
#include "rpcconn.H"
#include "rpcserver.H"

class controlserver;
class mastersecret;
template <typename> class orerror;
class peername;
class registrationsecret;

class coordinatorconn;

class coordinatorstatus {
public: list<rpcconn::status_t> conns;
public: coordinatorstatus(
    const list<rpcconn::status_t> &_conns)
    : conns(_conns) {}
public: ~coordinatorstatus() { conns.flush(); }
WIREPROTO_TYPE(coordinatorstatus);
};
namespace fields {
const field &mk(const coordinatorstatus &);
}

class coordinator : public rpcserver<coordinatorconn> {
    friend class coordinatorconn;
private: mastersecret ms;
private: registrationsecret rs;

    /* Control server interface */
private: class statusinterface : public ::statusinterface {
    private: coordinator *const owner;
    public:  statusinterface(
        coordinator *_owner)
        : owner(_owner) {}
    private: void getstatus(wireproto::tx_message *, mutex_t::token) const;
    };
private: statusinterface statusiface;
private: statusregistration controlregistration;

private: mutable mutex_t mux;
private: list<coordinatorconn *> connections;

public:  static orerror<coordinator *> build(
    const mastersecret &,
    const registrationsecret &,
    const peername &,
    controlserver *);
private: coordinator(const mastersecret &,
                     const registrationsecret &,
                     controlserver *);
private: orerror<coordinatorconn *> accept(socket_t);
public:  void destroy(clientio);
public:  typedef coordinatorstatus status_t;
public:  status_t status() const;
};

class coordinatorconnstatus;

namespace fields {
const field &mk(const coordinatorconnstatus &);
}

#endif /* !COORDINATOR_H__ */
