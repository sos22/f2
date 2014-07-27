#include "coordinator.H"

#include "fields.H"
#include "proto.H"
#include "logging.H"

#include "list.tmpl"
#include "rpcconn.tmpl"
#include "wireproto.tmpl"

#include "fieldfinal.H"

wireproto_wrapper_type(coordinatorstatus);

class coordinatorconnstatus {
    WIREPROTO_TYPE(coordinatorconnstatus);
public:  rpcconn::status_t connstatus;
public:  maybe<slavename> name;
public:  coordinatorconnstatus(const rpcconn::status_t &b,
                               const maybe<slavename> &s)
    : connstatus(b),
      name(s) {}
};
wireproto_wrapper_type(coordinatorconnstatus);
void
coordinatorconnstatus::addparam(
    wireproto::parameter<coordinatorconnstatus> tmpl,
    wireproto::tx_message &txm) const {
    wireproto::tx_compoundparameter p;
    p.addparam(proto::coordinatorconnstatus::conn, connstatus);
    if (name.isjust()) {
        p.addparam(proto::coordinatorconnstatus::slave, name.just()); }
    txm.addparam(wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
                 p); }
maybe<coordinatorconnstatus>
coordinatorconnstatus::fromcompound(const wireproto::rx_message &rxm) {
    auto c(rxm.getparam(proto::coordinatorconnstatus::conn));
    if (!c) return Nothing;
    return coordinatorconnstatus(
        c.just(),
        rxm.getparam(proto::coordinatorconnstatus::slave)); }

class coordinatorconn : public rpcconn {
public:  coordinator *const owner;
public:  coordinatorconn(socket_t &_socket,
                         const rpcconnauth &_auth,
                         peername &_peer,
                         coordinator *_owner);
private: void endconn(clientio);

public:  typedef coordinatorconnstatus status_t;
public:  status_t status(maybe<mutex_t::token> tok /* coordinator lock */) {
    return status_t( rpcconn::status(tok), slavename() ); }
};

coordinatorconn::coordinatorconn(socket_t &_socket,
                                 const rpcconnauth &__auth,
                                 peername &_peer,
                                 coordinator *_owner)
    : rpcconn(_socket, __auth, _peer),
      owner(_owner) {
    auto token(owner->mux.lock());
    owner->connections.pushtail(this);
    owner->mux.unlock(&token); }

void
coordinatorconn::endconn(clientio) {
    auto token(owner->mux.lock());
    bool found = false;
    for (auto it(owner->connections.start()); !it.finished(); it.next()) {
        if (*it == this) {
            found = true;
            it.remove();
            break; } }
    assert(found);
    owner->mux.unlock(&token); }

void
coordinator::statusinterface::getstatus(wireproto::tx_message *msg) const {
    msg->addparam(proto::STATUS::resp::coordinator, owner->status()); }

coordinator::status_t
coordinator::status() const {
    list<coordinatorconnstatus> c;
    auto token(mux.lock());
    for (auto it(connections.start()); !it.finished(); it.next()) {
        c.pushtail((*it)->status(token)); }
    mux.unlock(&token);
    coordinator::status_t res(c);
    c.flush();
    return res; }

coordinator::coordinator(
    const mastersecret &_ms,
    const registrationsecret &_rs,
    controlserver *cs)
    : ms(_ms),
      rs(_rs),
      statusiface(this, cs) { }

orerror<rpcconn *>
coordinator::accept(socket_t s) {
    return rpcconn::fromsocket<coordinatorconn>(
        s,
        rpcconnauth::mkwaithello(ms, rs),
        this); }

void
coordinator::destroy(clientio io) {
    statusiface.stop();
    rpcserver::destroy(io); }

orerror<coordinator *>
coordinator::build(
    const mastersecret &ms,
    const registrationsecret &rs,
    const peername &listenon,
    controlserver *cs) {
    auto res(new coordinator(ms, rs, cs));
    auto r(res->listen(listenon));
    if (r.isfailure()) {
        delete res;
        return r.failure(); }
    res->statusiface.start();
    return res; }

const fields::field &
fields::mk(const coordinatorconn::status_t &o) {
    return "<coordinatorconn " + mk(o.connstatus) +
        " name:" + mk(o.name) + ">"; }

void
coordinator::status_t::addparam(
    wireproto::parameter<coordinator::status_t> tmpl,
    wireproto::tx_message &tx_msg) const {
    tx_msg.addparam(wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
                    wireproto::tx_compoundparameter()
                    .addparam(proto::coordinatorstatus::conns, conns)); }
maybe<coordinator::status_t>
coordinator::status_t::fromcompound(const wireproto::rx_message &msg) {
    list<coordinatorconn::status_t> conns;
    auto r(msg.fetch(proto::coordinatorstatus::conns, conns));
    if (r.isfailure()) return Nothing;
    coordinator::status_t res(conns);
    conns.flush();
    return res; }
const fields::field &
fields::mk(const coordinator::status_t &o) {
    const field *res = &fields::mk("<conns:{");
    bool first = true;
    for (auto it(o.conns.start()); !it.finished(); it.next()) {
        if (!first) res = &(*res + ",");
        res = &(*res + mk(*it)); }
    return *res + "}>"; }
