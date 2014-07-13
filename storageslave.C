#include "storageslave.H"

#include "beaconclient.H"
#include "tcpsocket.H"

#include "list.tmpl"
#include "rpcconn.tmpl"
#include "wireproto.tmpl"

wireproto_wrapper_type(storageslave::status_t);

class storageslaveconn : public rpcconn {
    friend class rpcconn;
private: storageslave *const owner;
private: storageslaveconn(socket_t &_socket,
                          const rpcconnauth &_auth,
                          const peername &_peer,
                          storageslave *_owner);
private: void endconn(clientio);
};

storageslaveconn::storageslaveconn(
    socket_t &_socket,
    const rpcconnauth &_auth,
    const peername &_peer,
    storageslave *_owner)
    : rpcconn(_socket, _auth, _peer),
      owner(_owner) {
    auto token(owner->mux.lock());
    owner->clients.pushtail(this);
    owner->mux.unlock(&token); }

void
storageslaveconn::endconn(clientio) {
    auto token(owner->mux.lock());
    for (auto it(owner->clients.start()); true; it.next()) {
        if (*it == this) {
            it.remove();
            break; } }
    owner->mux.unlock(&token); }

void
storageslave::statusiface::getstatus(wireproto::tx_message *msg) const {
    msg->addparam(proto::STATUS::resp::storageslave, owner->status()); }

orerror<storageslave *>
storageslave::build(clientio io,
                    const registrationsecret &rs,
                    controlserver *cs) {
    auto br(beaconclient(rs));
    if (br.isfailure()) return br.failure();
    auto res(new storageslave(
                 br.success().secret,
                 cs));
    auto mc(rpcconn::connectmaster<storageslaveconn>(
                io,
                br.success(),
                res));
    if (mc.isfailure()) {
        delete res;
        return mc.failure(); }
    res->masterconn = mc.success();
    auto r(res->listen(peername::tcpany()));
    if (r.isjust()) {
        res->masterconn = NULL;
        delete res;
        mc.success()->destroy(io);
        return r.just(); }
    res->status_.start();
    return res; }

storageslave::storageslave(const registrationsecret &_rs,
                           controlserver *cs)
    : status_(this, cs),
      rs(_rs),
      masterconn(NULL),
      clients(),
      mux() {}

orerror<rpcconn *>
storageslave::accept(socket_t s) {
    return rpcconn::fromsocket<storageslaveconn>(
        s,
        rpcconnauth::mksendhelloslavea(rs),
        this); }

void
storageslave::destroy(clientio io) {
    status_.stop();
    /* Stop the master connection now, but don't release it until
       we've finished tearing down our clients.  Not clear whether the
       two-step is actually necessary, but it's a lot easier to think
       about than the synchronisation around a one-step. */
    masterconn->teardown();
    subscriber sub;
    subscription ss(sub, masterconn->deathpublisher());
    while (!masterconn->hasdied()) sub.wait();
    rpcserver::destroy(io); }

storageslave::~storageslave() {
    if (masterconn != NULL) {
        auto dt(masterconn->hasdied());
        /* Wait for master death before calling rpcconn::destroy(), so
           it should still be dead when we get here. */
        assert(dt != Nothing);
        masterconn->destroy(dt.just()); } }

storageslave::status_t
storageslave::status() const {
    assert(masterconn);
    auto token(mux.lock());
    status_t res(
        masterconn->status(token),
        clients.map<rpcconn::status_t>(
            [&token] (storageslaveconn *const &conn) {
                return conn->status(token); }));
    mux.unlock(&token);
    return res; }

void
storageslave::status_t::addparam(
    wireproto::parameter<storageslave::status_t> tmpl,
    wireproto::tx_message &tx_msg) const {
    tx_msg.addparam(wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
                    wireproto::tx_compoundparameter()
                    .addparam(proto::storageslavestatus::masterconn, masterconn)
                    .addparam(
                        proto::storageslavestatus::clientconns,
                        clientconns)); }
maybe<storageslave::status_t>
storageslave::status_t::fromcompound(const wireproto::rx_message &msg) {
    auto masterconn(msg.getparam(proto::storageslavestatus::masterconn));
    if (!masterconn) return Nothing;
    list<rpcconn::status_t> clientconns;
    auto r(msg.fetch(proto::storageslavestatus::clientconns, clientconns));
    if (r.isjust()) return Nothing;
    storageslave::status_t res(masterconn.just(), clientconns);
    clientconns.flush();
    return res; }
const fields::field &
fields::mk(const storageslave::status_t &o) {
    return "<storageslave: master=" + mk(o.masterconn) +
        " clients=" + mk(o.clientconns) + ">"; }
