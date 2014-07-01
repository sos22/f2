/* The coordinator is responsible for keeping track of what workers we
   currently have available in the pool and for managing coordination
   with them. */
#ifndef COORDINATOR_H__
#define COORDINATOR_H__

#include "rpcserver.H"

class controlserver;
class mastersecret;
template <typename> class orerror;
class peername;
class registrationsecret;

class coordinatorconn;

class coordinator : public rpcserver<coordinatorconn> {
public:  static orerror<coordinator *> build(
    const mastersecret &,
    const registrationsecret &,
    const peername &,
    controlserver *);
public:  virtual void destroy(clientio) = 0;
};

class coordinatorconnstatus;

namespace fields {
const field &mk(const coordinatorconnstatus &);
}

#endif /* !COORDINATOR_H__ */
