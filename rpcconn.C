#include "rpcconn.H"

#include "beaconclient.H"
#include "digest.H"
#include "fields.H"
#include "orerror.H"
#include "logging.H"
#include "proto.H"
#include "tcpsocket.H"

rpcconn::rpcconn(socket_t _fd)
    : outgoing(),
      incoming(),
      fd(_fd),
      sequencer(),
      pendingrx() {}

orerror<const wireproto::rx_message *>
rpcconn::_receive() {
    while (1) {
        auto r(wireproto::rx_message::fetch(incoming));
        if (r.issuccess()) return r.success();
        if (r.isfailure() && r.failure() != error::underflowed) {
            return r.failure(); }
        auto t(incoming.receive(fd));
        if (t.isjust()) return t.just(); } }

orerror<rpcconn *>
rpcconn::connect(const peername &p) {
    auto sock(tcpsocket::connect(p));
    if (sock.isfailure()) return sock.failure();
    else return new rpcconn(sock.success()); }

orerror<rpcconn *>
rpcconn::connectmaster(const beaconresult &beacon)
{
    logmsg(loglevel::verbose,
           "connect with slavename " + fields::mk(beacon.slavename));
    auto res(rpcconn::connect(beacon.mastername));
    if (res.isfailure()) return res.failure();
    auto snr(res.success()->allocsequencenr());
    auto hellores(res.success()->call(
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
    fd.close(); }

maybe<error>
rpcconn::send(const wireproto::tx_message &msg) {
    {   auto r(msg.serialise(outgoing));
        if (r.isjust()) return r; }
    
    while (!outgoing.empty()) {
        auto r(outgoing.send(fd));
        if (r.isjust()) return r; }
    return Nothing; }

orerror<const wireproto::rx_message *>
rpcconn::call(const wireproto::req_message &msg) {
    auto r = send(msg);
    if (r.isjust())
        return r.just();
    while (1) {
        auto m = _receive();
        if (m.isfailure())
            return m;
        if (m.success()->sequence == msg.sequence.reply())
            return m;
        pendingrx.pushtail(m.success()); } }

wireproto::sequencenr
rpcconn::allocsequencenr(void) {
    return sequencer.get(); }

void
rpcconn::putsequencenr(wireproto::sequencenr snr) {
    sequencer.put(snr); }

peername
rpcconn::peer() const {
    return fd.peer(); }
