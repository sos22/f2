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

class coordinatorconn : public rpcconn {
public:  coordinator *owner;
public:  coordinatorconn(socket_t &_socket, peername &_peer)
    : rpcconn(_socket, _peer),
      owner(NULL) {}
public:  messageresult message(const wireproto::rx_message &);
private: void endconn(clientio);
};

messageresult
coordinatorconn::message(const wireproto::rx_message &rxm) {
    if (!receivedhello) {
        if (rxm.tag() != proto::HELLO::tag) return error::unrecognisedmessage;
        auto version(rxm.getparam(proto::HELLO::req::version));
        auto nonce(rxm.getparam(proto::HELLO::req::nonce));
        auto slavename(rxm.getparam(proto::HELLO::req::slavename));
        auto digest(rxm.getparam(proto::HELLO::req::digest));
        if (!version || !nonce || !slavename || !digest) {
            return error::missingparameter; }
        logmsg(loglevel::verbose,
               "HELLO version " + fields::mk(version) +
               "nonce " + fields::mk(nonce) +
               "slavename " + fields::mk(slavename) +
               "digest " + fields::mk(digest));
        if (version.just() != 1) return error::badversion;
        if (!owner->ms.noncevalid(nonce.just(), slavename.just())) {
            logmsg(loglevel::notice,
                   "HELLO with invalid nonce from " + fields::mk(peer()));
            return error::authenticationfailed; }
        if (!slavename.just().samehost(peer())) {
            logmsg(loglevel::notice,
                   "HELLO with bad host (" + fields::mk(slavename.just()) +
                   ", expected host " + fields::mk(peer()) +")");
            return error::authenticationfailed; }
        if (digest.just() != ::digest("B" +
                                      fields::mk(nonce.just()) +
                                      fields::mk(owner->rs))) {
            logmsg(loglevel::notice,
                   "HELLO with invalid digest from " + fields::mk(peer()));
            return error::authenticationfailed; }
        logmsg(loglevel::notice, "Valid HELLO from " + fields::mk(peer()));
        receivedhello = true;
        return new wireproto::resp_message(rxm); }

    return rpcconn::message(rxm); }

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
    list<rpcconn::status_t> c;
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

orerror<coordinatorconn *>
coordinator::accept(socket_t s) {
    auto res(rpcconn::fromsocket<coordinatorconn>(s));
    if (res.issuccess()) {
        res.success()->owner = this;
        auto token(mux.lock());
        connections.pushtail(res.success());
        mux.unlock(&token); }
    return res; }

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
    if (r.isjust()) {
        delete res;
        return r.just(); }
    res->statusiface.start();
    return res; }

void
coordinator::status_t::addparam(
    wireproto::parameter<coordinator::status_t> tmpl,
    wireproto::tx_message &tx_msg) const {
    tx_msg.addparam(wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
                    wireproto::tx_compoundparameter()
                    .addparam(proto::coordinatorstatus::conns, conns)); }
maybe<coordinator::status_t>
coordinator::status_t::fromcompound(const wireproto::rx_message &msg) {
    list<rpcconn::status_t> conns;
    auto r(msg.fetch(proto::coordinatorstatus::conns, conns));
    if (r.isjust()) return Nothing;
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
