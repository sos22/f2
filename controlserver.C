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
#include "waitqueue.H"
#include "wireproto.H"

#include "list.tmpl"
#include "waitqueue.tmpl"

#include "fieldfinal.H"

namespace _controlserver {

class pingiface : public rpcinterface {
public:  pingiface() : rpcinterface(proto::PING::tag) {}
private: maybe<error> message(const wireproto::rx_message &,
                              rpcconn &,
                              buffer &);
};
class quitiface : public rpcinterface {
    waitbox<shutdowncode> *s;
    quitiface() = delete;
    quitiface(const quitiface &) = delete;
    void operator=(const quitiface &) = delete;
public:  quitiface(waitbox<shutdowncode> *_s)
    : rpcinterface(proto::QUIT::tag),
      s(_s) {}
private: maybe<error> message(const wireproto::rx_message &,
                              rpcconn &,
                              buffer &);
};

class controlserverimpl : public controlserver {
private: pingiface pinginterface;
private: quitiface quitinterface;
private: rpcregistration *registration;
    
public:  controlserverimpl(waitbox<shutdowncode> *s)
    : controlserver(),
      pinginterface(),
      quitinterface(s),
      registration(registeriface(multiregistration()
                                 .add(getlogsiface::singleton)
                                 .add(pinginterface)
                                 .add(quitinterface))) {}
private: controlserverimpl(const controlserverimpl &) = delete;
private: void operator=(const controlserverimpl &) = delete;

public:  maybe<error> setup(const peername &);
public:  void destroy();
};

maybe<error>
pingiface::message(const wireproto::rx_message &msg,
                   rpcconn &conn,
                   buffer &outgoing)
{
    logmsg(loglevel::info,
           "ping msg " + fields::mk(msg.getparam(proto::PING::req::msg)) +
           "from " + fields::mk(conn.peer()));
    wireproto::resp_message m(msg);
    static int cntr;
    m.addparam(proto::PING::resp::cntr, cntr++);
    m.addparam(proto::PING::resp::msg, "response message");
    auto r(m.serialise(outgoing));
    if (r.isjust())
        r.just().warn("sending pong");
    return r;
}

maybe<error>
quitiface::message(const wireproto::rx_message &msg,
                   rpcconn &conn,
                   buffer &) {
    auto reason(msg.getparam(proto::QUIT::req::reason));
    if (!reason) return error::missingparameter;
    auto str(msg.getparam(proto::QUIT::req::message));
    logmsg(loglevel::notice,
           "received a quit message: " + fields::mk(str) +
           "from " + fields::mk(conn.peer()));
    s->set(reason.just());
    return Nothing;
}

/* not a destructor because it has non-trivial synchronisation rules. */
void
controlserverimpl::destroy() {
    stop();
    registration->destroy();
    rpcserver::destroy();
}

maybe<error>
controlserverimpl::setup(
    const peername &p) {
    return start(p, fields::mk("control server")); }

} /* end of _controlserver namespace */

/* The controlserver class itself is just a wrapper around
   controlserverimpl, so that we don't need all the implementation
   details in the controlserver.H header. */
orerror<controlserver *>
controlserver::build(const peername &p, waitbox<shutdowncode> *s)
{
    auto r(new _controlserver::controlserverimpl(s));
    auto e(r->setup(p));
    if (e.isnothing()) {
        return r;
    } else {
        r->destroy();
        return e.just(); } }
