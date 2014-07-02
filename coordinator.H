/* The coordinator is responsible for keeping track of what workers we
   currently have available in the pool and for managing coordination
   with them. */
#ifndef COORDINATOR_H__
#define COORDINATOR_H__

#include "ratelimiter.H"
#include "rpcserver.H"

class controlserver;
class mastersecret;
template <typename> class orerror;
class peername;
class registrationsecret;

class coordinatorconn;

class coordinatorconnstatus {
public: int active;
public: struct timeval lastcontact;
public: rpcconn::status_t conn;
public: coordinatorconnstatus(int _active,
                              struct timeval _lastcontact,
                              const rpcconn::status_t &_conn)
    : active(_active),
      lastcontact(_lastcontact),
      conn(_conn) {}
public: static maybe<coordinatorconnstatus> fromcompound(
    const wireproto::rx_message &);
WIREPROTO_TYPE(coordinatorconnstatus);
};

class coordinatorstatus {
public: ratelimiter_status limiter;
public: list<coordinatorconnstatus> conns;
public: coordinatorstatus(
    const ratelimiter_status &_limiter,
    const list<coordinatorconnstatus> &_conns)
    : limiter(_limiter), conns(_conns) {}
public: ~coordinatorstatus() { conns.flush(); }
WIREPROTO_TYPE(coordinatorstatus);
};
namespace fields {
const field &mk(const coordinatorstatus &);
}

class coordinator : public rpcserver<coordinatorconn> {
public:  static orerror<coordinator *> build(
    const mastersecret &,
    const registrationsecret &,
    const peername &,
    controlserver *);
public:  virtual void destroy(clientio) = 0;
public:  typedef coordinatorstatus status_t;
public:  virtual status_t status() const = 0;
};

class coordinatorconnstatus;

namespace fields {
const field &mk(const coordinatorconnstatus &);
}

#endif /* !COORDINATOR_H__ */
