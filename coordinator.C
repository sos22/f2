#include "coordinator.H"

#include "fields.H"
#include "proto.H"
#include "logging.H"

#include "list.tmpl"
#include "rpcconn.tmpl"
#include "rpcserver.tmpl"
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
    friend class pausedthread<coordinatorconn>;
public:  coordinator *const owner;
public:  coordinatorconn(const rpcconn::rpcconntoken &token,
                         coordinator *_owner);
private: void endconn(clientio);

public:  typedef coordinatorconnstatus status_t;
public:  status_t status(maybe<mutex_t::token> tok /* coordinator lock */) {
    return status_t( rpcconn::status(tok), slavename() ); }
};

coordinatorconn::coordinatorconn(const rpcconn::rpcconntoken &tok,
                                 coordinator *_owner)
    : rpcconn(tok),
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
coordinator::controlinterface::getstatus(wireproto::tx_message *msg) const {
    msg->addparam(proto::STATUS::resp::coordinator, owner->status()); }

void
coordinator::controlinterface::getlistening(
    wireproto::resp_message *msg) const {
    msg->addparam(proto::LISTENING::resp::coordinator, owner->localname()); }

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
    constoken token,
    listenfd fd,
    const mastersecret &_ms,
    const registrationsecret &_rs,
    controlserver *cs)
    : rpcserver(token, fd),
      ms(_ms),
      rs(_rs),
      controliface(this, cs) {
    controliface.start(); }

orerror<rpcconn *>
coordinator::accept(socket_t s) {
    return rpcconn::fromsocket<coordinatorconn>(
        s,
        rpcconnauth::mkwaithello(ms, rs, rpcconnconfig::dflt),
        rpcconnconfig::dflt,
        this); }

void
coordinator::destroy(clientio io) {
    controliface.stop();
    rpcserver::destroy(io); }

orerror<coordinator *>
coordinator::build(
    const mastersecret &ms,
    const registrationsecret &rs,
    const peername &listenon,
    controlserver *cs) {
    return rpcserver::listen<coordinator>(listenon, ms, rs, cs)
        .map<coordinator *>([] (pausedrpcserver<coordinator> c) {
                return c.go(); }); }

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
