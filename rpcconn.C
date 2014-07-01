#include "rpcconn.H"

#include "beaconclient.H"
#include "digest.H"
#include "fields.H"
#include "orerror.H"
#include "logging.H"
#include "proto.H"
#include "pubsub.H"
#include "tcpsocket.H"

#include "either.tmpl"
#include "list.tmpl"
#include "wireproto.tmpl"

rpcconn::rpcconn(socket_t _fd, const peername &_peer)
    : txlock(),
      outgoing(),
      rxlock(),
      incoming(),
      fd(_fd),
      sequencerlock(),
      sequencer(),
      pendingrx(),
      peer_(_peer) {}

orerror<rpcconn::receiveres>
rpcconn::_receive(
    clientio io,
    mutex_t::token token,
    subscriber &sub,
    maybe<timestamp> deadline) {
    token.formux(rxlock);
    while (1) {
        auto r(wireproto::rx_message::fetch(incoming));
        if (r.issuccess()) return receiveres(r.success().steal());
        if (r.isfailure() && r.failure() != error::underflowed) {
            return r.failure(); }
        auto t(incoming.receive(io, fd, sub, deadline));
        if (t.isfailure()) return t.failure();
        if (t.success()) return receiveres(t.success()); } }

orerror<const wireproto::rx_message *>
rpcconn::receive(clientio io, maybe<timestamp> deadline) {
    auto token(rxlock.lock());
    if (!pendingrx.empty()) {
        auto res(pendingrx.pophead());
        rxlock.unlock(&token);
        return res; }
    subscriber sub;
    auto r(_receive(io, token, sub, deadline));
    rxlock.unlock(&token);
    if (r.isfailure()) return r.failure();
    assert(r.success().ismessage());
    return r.success().message(); }

orerror<rpcconn::receiveres>
rpcconn::receive(clientio io,
                 subscriber &sub,
                 wireproto::sequencenr snr,
                 maybe<timestamp> deadline) {
    auto token(rxlock.lock());
    for (auto it(pendingrx.start()); !it.finished(); it.next()) {
        if ((*it)->sequence() == snr) {
            auto res(*it);
            it.remove();
            rxlock.unlock(&token);
            return receiveres(res); } }
    while (1) {
        auto m = _receive(io, token, sub, deadline);
        if (m.isfailure()) {
            rxlock.unlock(&token);
            return m.failure(); }
        if (m.success().issubscription()) {
            rxlock.unlock(&token);
            return receiveres(m.success().subscription()); }
        if (m.success().message()->sequence() == snr) {
            rxlock.unlock(&token);
            return receiveres(m.success().message()); }
        pendingrx.pushtail(m.success().message()); } }

orerror<rpcconn *>
rpcconn::connect(clientio io, const peername &p) {
    auto sock(tcpsocket::connect(io, p));
    if (sock.isfailure()) return sock.failure();
    else return new rpcconn(sock.success(), p); }

orerror<rpcconn *>
rpcconn::connectmaster(clientio io, const beaconresult &beacon)
{
    logmsg(loglevel::verbose,
           "connect with slavename " + fields::mk(beacon.slavename));
    auto res(rpcconn::connect(io, beacon.mastername));
    if (res.isfailure()) return res.failure();
    auto snr(res.success()->allocsequencenr());
    auto hellores(res.success()->call(
                      io,
                      wireproto::req_message(proto::HELLO::tag, snr)
                      .addparam(proto::HELLO::req::version, 1u)
                      .addparam(proto::HELLO::req::nonce, beacon.nonce)
                      .addparam(proto::HELLO::req::slavename, beacon.slavename)
                      .addparam(proto::HELLO::req::digest,
                                digest("B" +
                                       fields::mk(beacon.nonce) +
                                       fields::mk(beacon.secret)))));
    res.success()->putsequencenr(snr);
    if (hellores.isfailure()) {
        delete res.success();
        return hellores.failure(); }
    logmsg(loglevel::notice,
           "connected to master at " + fields::mk(beacon.mastername));
    return res.success(); }

rpcconn::~rpcconn() {
    fd.close();
    while (!pendingrx.empty()) delete pendingrx.pophead(); }

maybe<error>
rpcconn::send(
    clientio io,
    const wireproto::tx_message &msg,
    maybe<timestamp> deadline) {
    auto token(txlock.lock());
    {   auto r(msg.serialise(outgoing));
        if (r.isjust()) {
            txlock.unlock(&token);
            return r; } }
    
    /* XXX flushing out the entire buffer is a bit stupid; all we
       really need to do is flush out the message we just added.
       Going a bit past that can sometimes help a bit, if it means you
       get to send several messages at once.  Continuing to the end of
       the buffer isn't, because it induces additional wait graph
       edges when lots of threads have queued things up to send. */
    while (!outgoing.empty()) {
        auto r(outgoing.send(io, fd, deadline));
        if (r.isjust()) {
            txlock.unlock(&token);
            return r; } }
    txlock.unlock(&token);
    return Nothing; }

orerror<const wireproto::rx_message *>
rpcconn::call(clientio io, const wireproto::req_message &msg) {
    auto r = send(io, msg);
    if (r.isjust()) return r.just();
    subscriber sub;
    auto rr(receive(io, sub, msg.sequence.reply()));
    if (rr.isfailure()) return rr.failure();
    assert(rr.success().ismessage());
    return rr.success().message(); }

wireproto::sequencenr
rpcconn::allocsequencenr(void) {
    auto token(sequencerlock.lock());
    auto res(sequencer.get());
    sequencerlock.unlock(&token);
    return res; }

void
rpcconn::putsequencenr(wireproto::sequencenr snr) {
    auto token(sequencerlock.lock());
    sequencer.put(snr);
    sequencerlock.unlock(&token); }

peername
rpcconn::peer() const {
    return peer_; }

rpcconn::status_t
rpcconn::status(maybe<mutex_t::token> /*coordinatorlock*/) const {
    list<wireproto::rx_message::status_t> prx(
        pendingrx.map<wireproto::rx_message::status_t>(
            [] (const wireproto::rx_message *elem) {
                return elem->status(); }));
    rpcconn::status_t res(
        outgoing.status(),
        incoming.status(),
        fd.status(),
        sequencer.status(),
        prx,
        peer_.status());
    prx.flush();
    return res; }

const fields::field &
fields::mk(const rpcconn::status_t &o) {
    return
        "<outgoing:" + mk(o.outgoing) +
        " incoming:" + mk(o.incoming) +
        " fd:" + mk(o.fd) +
        " sequencer:" + mk(o.sequencer) +
        " pendingrx:" + mk(o.pendingrx) +
        " peername:" + mk(o.peername_) +
        ">"; }

wireproto_wrapper_type(rpcconn::status_t)
void
rpcconn::status_t::addparam(
    wireproto::parameter<rpcconn::status_t> tmpl,
    wireproto::tx_message &out) const {
    out.addparam(
        wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
        wireproto::tx_compoundparameter()
        .addparam(proto::rpcconnstatus::outgoing, outgoing)
        .addparam(proto::rpcconnstatus::incoming, incoming)
        .addparam(proto::rpcconnstatus::fd, fd)
        .addparam(proto::rpcconnstatus::sequencer, sequencer)
        .addparam(proto::rpcconnstatus::pendingrx, pendingrx)
        .addparam(proto::rpcconnstatus::peername, peername_)); }
maybe<rpcconn::status_t>
rpcconn::status_t::getparam(
    wireproto::parameter<rpcconn::status_t> tmpl,
    const wireproto::rx_message &msg) {
    auto packed(msg.getparam(
                    wireproto::parameter<wireproto::rx_message>(tmpl)));
    if (!packed) return Nothing;
    auto &p(packed.just());
#define doparam(name) auto name(p.getparam(proto::rpcconnstatus::name))
    doparam(outgoing);
    doparam(incoming);
    doparam(fd);
    doparam(sequencer);
    doparam(peername);
#undef doparam
    if (!outgoing || !incoming || !fd || !sequencer || !peername) {
        return Nothing; }
    list<wireproto::rx_messagestatus> pendingrx;
    auto r(p.fetch(proto::rpcconnstatus::pendingrx, pendingrx));
    if (r.isjust()) return Nothing;
    rpcconn::status_t res(outgoing.just(),
                          incoming.just(),
                          fd.just(),
                          sequencer.just(),
                          pendingrx,
                          peername.just());
    pendingrx.flush();
    return res; }

template class either<subscriptionbase *, const wireproto::rx_message *>;
template list<wireproto::rx_message::status_t>
    list<wireproto::rx_message const*>::map<wireproto::rx_message::status_t>(
        std::function<wireproto::rx_messagestatus
                      (wireproto::rx_message const* const&)>) const;
