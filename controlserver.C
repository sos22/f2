#include "controlserver.H"

#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "buffer.H"
#include "cond.H"
#include "fd.H"
#include "fields.H"
#include "list.H"
#include "listenfd.H"
#include "logging.H"
#include "maybe.H"
#include "mutex.H"
#include "thread.H"
#include "orerror.H"
#include "peername.H"
#include "proto.H"
#include "rpcconn.H"
#include "socket.H"
#include "shutdown.H"
#include "unixsocket.H"
#include "util.H"
#include "waitbox.H"
#include "wireproto.H"

#include "list.tmpl"
#include "rpcserver.tmpl"

#include "fieldfinal.H"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

class controlconn {
public: peername peer;
public: _controlserver::controlserverimpl *owner;
public: controlconn(_controlserver::controlserverimpl *_owner,
                    const peername &p)
    : peer(p),
      owner(_owner) {} };

namespace _controlserver {

class controlserverimpl;

class pingiface : public rpcinterface<controlconn> {
public:  pingiface() : rpcinterface(proto::PING::tag) {}
private: messageresult message(
    const wireproto::rx_message &,
    controlconn *);
};
class quitiface : public rpcinterface<controlconn> {
    waitbox<shutdowncode> &s;
    quitiface() = delete;
    quitiface(const quitiface &) = delete;
    void operator=(const quitiface &) = delete;
public:  quitiface(waitbox<shutdowncode> &_s)
    : rpcinterface(proto::QUIT::tag),
      s(_s) {}
private: messageresult message(
    const wireproto::rx_message &,
    controlconn *);
};

class statusiface : public rpcinterface<controlconn> {
private: controlserverimpl *owner;
public:  statusiface(controlserverimpl *_owner)
    : rpcinterface<controlconn>(proto::STATUS::tag),
      owner(_owner) {}
private: messageresult message(
    const wireproto::rx_message &,
    controlconn *);
};
class controlserverimpl : public controlserver {
    friend class ::statusregistration;
    friend class statusiface;
private: pingiface pinginterface;
private: quitiface quitinterface;
private: statusiface statusinterface_;
private: rpcregistration<controlconn> *registration;
private: list<statusinterface *> statusifaces;
private: mutex_t statuslock;

public:  controlserverimpl(waitbox<shutdowncode> &s)
    : controlserver(),
      pinginterface(),
      quitinterface(s),
      statusinterface_(this),
      registration(service->registeriface(
                       rpcservice<controlconn>::multiregistration()
                       .add(getlogsiface::singleton)
                       .add(statusinterface_)
                       .add(pinginterface)
                       .add(quitinterface))) {}
private: controlserverimpl(const controlserverimpl &) = delete;
private: void operator=(const controlserverimpl &) = delete;

private: orerror<controlconn *> startconn(clientio, rpcconn &);
private: void endconn(clientio, controlconn *);

public:  maybe<error> setup(const peername &);
public:  statusregistration registeriface(statusinterface &);
public:  void destroy(clientio);
};

messageresult
pingiface::message(const wireproto::rx_message &msg,
                   controlconn *conn)
{
    logmsg(loglevel::info,
           "ping msg " + fields::mk(msg.getparam(proto::PING::req::msg)) +
           " from " + fields::mk(conn->peer));
    auto m(new wireproto::resp_message(msg));
    static int cntr;
    m->addparam(proto::PING::resp::cntr, cntr++);
    m->addparam(proto::PING::resp::msg, "response message");
    return m; }

messageresult
quitiface::message(const wireproto::rx_message &msg,
                   controlconn *conn) {
    auto reason(msg.getparam(proto::QUIT::req::reason));
    if (!reason) return error::missingparameter;
    auto str(msg.getparam(proto::QUIT::req::message));
    logmsg(loglevel::notice,
           "received a quit message: " + fields::mk(str) +
           "from " + fields::mk(conn->peer));
    s.set(reason.just());
    return messageresult::noreply; }

messageresult
statusiface::message(const wireproto::rx_message &msg, controlconn *conn) {
    auto res(new wireproto::resp_message(msg));
    auto token(conn->owner->statuslock.lock());
    for (auto it(conn->owner->statusifaces.start());
         !it.finished();
         it.next()) {
        (*it)->getstatus(res, token); }
    conn->owner->statuslock.unlock(&token);
    return res; }

/* not a destructor because it has non-trivial synchronisation rules. */
void
controlserverimpl::destroy(clientio io) {
    stop(io);
    rpcserver::destroy(io);
}

orerror<controlconn *>
controlserverimpl::startconn(clientio, rpcconn &conn) {
    return new controlconn(this, conn.peer()); }

void
controlserverimpl::endconn(clientio, controlconn *conn) {
    delete conn; }

maybe<error>
controlserverimpl::setup(
    const peername &p) {
    return start(p, fields::mk("control server")); }

statusregistration
controlserverimpl::registeriface(statusinterface &what) {
    auto token(statuslock.lock());
    statusifaces.pushtail(&what);
    statuslock.unlock(&token);
    return statusregistration(&what, this); }

} /* end of _controlserver namespace */

/* The controlserver class itself is just a wrapper around
   controlserverimpl, so that we don't need all the implementation
   details in the controlserver.H header. */
orerror<controlserver *>
controlserver::build(const peername &p, waitbox<shutdowncode> &s)
{
    auto r(new _controlserver::controlserverimpl(s));
    auto e(r->setup(p));
    if (e.isnothing()) {
        return r;
    } else {
        /* This can't block for very long, because it's really just a
           destructor, so doesn't need a clientio */
        r->destroy(clientio::CLIENTIO);
        return e.just(); } }

statusregistration::statusregistration(
    statusinterface *_what,
    _controlserver::controlserverimpl *_owner)
    : what(_what), owner(_owner) {}

void
statusregistration::unregister() {
    auto token(owner->statuslock.lock());
    bool found = false;
    for (auto it(owner->statusifaces.start()); !it.finished(); it.next()) {
        if (*it == what) {
            found = true;
            it.remove();
            break; } }
    assert(found);
    owner->statuslock.unlock(&token); }

RPCSERVER(controlconn)

template class list<statusinterface*>;
