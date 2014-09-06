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

class actortype;
class controlserver;
class mastersecret;
template <typename> class orerror;
class peername;
class registrationsecret;

class coordinatorconn;
class coordinatorconnstatus;

class coordinatorconnstatus {
    WIREPROTO_TYPE(coordinatorconnstatus);
public:  rpcconn::status_t connstatus;
public:  maybe<slavename> name;
public:  coordinatorconnstatus(const rpcconn::status_t &b,
                               const maybe<slavename> &s)
    : connstatus(b),
      name(s) {}
};

class coordinatorstatus {
public: list<coordinatorconnstatus> conns;
public: coordinatorstatus(
    const list<coordinatorconnstatus> &_conns)
    : conns(_conns) {}
public: ~coordinatorstatus() { conns.flush(); }
WIREPROTO_TYPE(coordinatorstatus);
};
namespace fields {
const field &mk(const coordinatorstatus &);
}

class coordinator : public rpcserver {
    friend class coordinatorconn;
    friend class pausedthread<coordinator>;
    friend class thread;
private: mastersecret ms;
private: registrationsecret rs;
private: rpcconnconfig connconfig;

    /* Control server interface */
private: class controlinterface : public ::controlinterface {
    private: coordinator *const owner;
    public:  controlinterface(
        coordinator *_owner,
        controlserver *cs)
        : ::controlinterface(cs),
          owner(_owner) {}
    private: void getstatus(wireproto::tx_message *) const;
    private: void getlistening(wireproto::resp_message *) const;
    };
private: controlinterface controliface;

    /* Above the connection auth locks */
private: mutable mutex_t mux;
private: list<coordinatorconn *> connections;
    /* Notified whenever a slave enters the connected state (i.e. when
     * it starts being exposed to the slave iterators). */
public:  publisher slaveconnected;

public:  static orerror<coordinator *> build(
    const mastersecret &,
    const registrationsecret &,
    const peername &,
    controlserver *,
    const rpcconnconfig &);
private: coordinator(constoken,
                     listenfd,
                     const mastersecret &,
                     const registrationsecret &,
                     controlserver *,
                     const rpcconnconfig &);
private: orerror<rpcconn *> accept(socket_t);

    /* Enumerator for connected slaves.  A slave is added to the
     * connected list once it's made it through the authentication
     * protocol (and the slaveconnected publisher will be notified
     * immediately afterwards).  Slaves are removed from the connected
     * list when the connection is lost.  It is (obviously) possible
     * for the connection to be lost before the slave is removed from
     * the list (because connection loss can happen at any time, with
     * no synchronisation, depending on hardware error).  It is also
     * possible for callers to see the slave drop out of the list
     * before they see the connection lost if the slave transiently
     * reconnects after we detect the initial connection loss. */
    /* Note that enumerating the slave list does not prevent slaves
     * from arriving or leaving.  The guarantees we provide are:
     *
     * (a) Any slave which is in the list when the enumeration starts
     *     and remains in the list until it finishes will be returned at
     *     some point in the enumeration.
     * (b) Any slave which is returned by the enumeration will have been
     *     in the list at some poing between the enumeration starting and
     *     it finishing.
     * (c) The enumerator returns each slave at most once.
     */
    /* The implementation actually works by snapshotting the set of
     * available connections when it starts, but we'll probably want
     * to switch to something a bit more intricate at some point. */
public:  class iterator {
        friend class coordinator;
    private: list<slavename> content;
    private: list<slavename>::iter cursor;
    private: iterator(const coordinator *, actortype);
    public:  slavename get() const;
    public:  void next();
    public:  bool finished() const;
    public:  ~iterator(); };
public:  iterator start(actortype) const;

public:  void destroy(clientio);
public:  typedef coordinatorstatus status_t;
public:  status_t status() const;
};

class coordinatorconnstatus;

namespace fields {
const field &mk(const coordinatorconnstatus &);
}

namespace tests {
void _coordinator(); }

#endif /* !COORDINATOR_H__ */
