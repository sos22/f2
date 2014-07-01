#ifndef STORAGESLAVE_H__
#define STORAGESLAVE_H__

#include "rpcconn.H"

class clientio;
class controlserver;
template <typename> class orerror;
class registrationsecret;

class storageslave : public rpcconn {
    friend class rpcconn;
private: storageslave(socket_t &_socket, const peername &_peer)
    : rpcconn(_socket, _peer) {}
public:  static orerror<storageslave *> build(
    clientio io,
    const registrationsecret &rs,
    controlserver *);
};

#endif /* !STORAGESLAVE_H__ */
