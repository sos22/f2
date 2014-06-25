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

rpcconn::rpcconn(socket_t _fd, const peername &_peer)
    : outgoing(),
      incoming(),
      fd(_fd),
      sequencer(),
      pendingrx(),
      peer_(_peer) {}

orerror<rpcconn::receiveres>
rpcconn::_receive(clientio io, subscriber &sub, maybe<timestamp> deadline) {
    while (1) {
        auto r(wireproto::rx_message::fetch(incoming));
        if (r.issuccess()) return receiveres(r.success());
        if (r.isfailure() && r.failure() != error::underflowed) {
            return r.failure(); }
        auto t(incoming.receive(io, fd, sub, deadline));
        if (t.isfailure()) return t.failure();
        if (t.success()) return receiveres(t.success()); } }

orerror<const wireproto::rx_message *>
rpcconn::receive(clientio io, maybe<timestamp> deadline) {
    if (!pendingrx.empty()) return pendingrx.pophead();
    subscriber sub;
    auto r(_receive(io, sub, deadline));
    if (r.isfailure()) return r.failure();
    assert(r.success().ismessage());
    return r.success().message(); }

orerror<rpcconn::receiveres>
rpcconn::receive(clientio io,
                 subscriber &sub,
                 wireproto::sequencenr snr,
                 maybe<timestamp> deadline) {
    for (auto it(pendingrx.start()); !it.finished(); it.next()) {
        if ((*it)->sequence == snr) {
            auto res(*it);
            it.remove();
            return receiveres(res); } }
    while (1) {
        auto m = _receive(io, sub, deadline);
        if (m.isfailure()) return m.failure();
        if (m.success().issubscription()) {
            return receiveres(m.success().subscription()); }
        if (m.success().message()->sequence == snr) {
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
    while (!pendingrx.empty())
        pendingrx.pophead()->finish(); }

maybe<error>
rpcconn::send(
    clientio io,
    const wireproto::tx_message &msg,
    maybe<timestamp> deadline) {
    {   auto r(msg.serialise(outgoing));
        if (r.isjust()) return r; }
    
    while (!outgoing.empty()) {
        auto r(outgoing.send(io, fd, deadline));
        if (r.isjust()) return r; }
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
    return sequencer.get(); }

void
rpcconn::putsequencenr(wireproto::sequencenr snr) {
    sequencer.put(snr); }

peername
rpcconn::peer() const {
    return peer_; }

template class either<subscriptionbase *, const wireproto::rx_message *>;
