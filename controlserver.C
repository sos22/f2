#include "controlserver.H"

#include "fields.H"
#include "logging.H"
#include "proto.H"
#include "rpcconn.H"
#include "shutdown.H"

#include "list.tmpl"
#include "rpcconn.tmpl"
#include "rpcserver.tmpl"

class controlconn : public rpcconn {
public: controlserver *owner;
public: controlconn(socket_t _sock,
                    const peername &_peer)
    : rpcconn(_sock, _peer),
      owner(NULL) {}
public: messageresult message(const wireproto::rx_message &);
public: ~controlconn() {}
};

messageresult
controlconn::message(const wireproto::rx_message &rxm) {
    if (rxm.tag() == proto::QUIT::tag) {
        auto reason(rxm.getparam(proto::QUIT::req::reason));
        auto msg(rxm.getparam(proto::QUIT::req::message));
        if (!reason || !msg) return error::missingparameter;
        if (!owner->shutdown.ready()) {
            logmsg(loglevel::notice,
                   "received a quit message: " + fields::mk(msg.just()) +
                   " from " + fields::mk(peer()));
            owner->shutdown.set(reason.just());
        } else {
            logmsg(loglevel::notice,
                   "reject too-late shutdown from " + fields::mk(peer())); }
        return messageresult::noreply;
    } else if (rxm.tag() == proto::STATUS::tag) {
        auto res(new wireproto::resp_message(rxm));
        auto token(owner->statuslock.lock());
        for (auto it(owner->statusifaces.start());
             !it.finished();
             it.next()) {
            (*it)->getstatus(res, token); }
        owner->statuslock.unlock(&token);
        return res;
    } else if (rxm.tag() == proto::GETLOGS::tag) {
        return getlogs(rxm);
    } else {
        return rpcconn::message(rxm); } }

controlserver::controlserver(waitbox<shutdowncode> &_shutdown)
    : shutdown(_shutdown) {}

orerror<controlconn *>
controlserver::accept(socket_t s) {
    auto r(rpcconn::fromsocket<controlconn>(s));
    if (r.issuccess()) r.success()->owner = this;
    return r; }

statusregistration
controlserver::registeriface(statusinterface &what) {
    auto token(statuslock.lock());
    statusifaces.pushtail(&what);
    statuslock.unlock(&token);
    return statusregistration(&what, this); }

orerror<controlserver *>
controlserver::build(const peername &p, waitbox<shutdowncode> &s)
{
    auto r(new controlserver(s));
    auto e(r->listen(p));
    if (e.isnothing()) {
        return r;
    } else {
        delete r;
        return e.just(); } }

statusregistration::statusregistration(
    statusinterface *_what,
    controlserver *_owner)
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

template orerror<controlconn*> rpcconn::fromsocket<controlconn>(socket_t);
template class list<rpcserver<controlconn>::connsub*>;
template class list<statusinterface *>;
template class rpcserver<controlconn>;
