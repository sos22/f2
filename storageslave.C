#include "storageslave.H"

#include "beaconclient.H"
#include "buffer.H"
#include "controlserver.H"
#include "fields.H"
#include "logging.H"
#include "mastersecret.H"
#include "nonce.H"
#include "orerror.H"
#include "rpcconn.H"
#include "peername.H"
#include "registrationsecret.H"
#include "rpcservice.H"
#include "tcpsocket.H"
#include "timedelta.H"
#include "timestamp.H"
#include "udpsocket.H"
#include "wireproto.H"

#include "rpcconnthread.tmpl"
#include "rpcregistration.tmpl"
#include "rpcservice.tmpl"

class storageslavectxt {
};

messageresult
storageslave::pingiface::message(const wireproto::rx_message &msg,
                                 storageslavectxt *) {
    logmsg(loglevel::info, fields::mk("storage ping"));
    return new wireproto::resp_message(msg); }

storageslave::storageslave(controlserver *cs)
    : pinginterface(),
      statusinterface(this),
      controlregistration(
          cs->service->registeriface(statusinterface)),
      service(new rpcservice<storageslavectxt *>()),
      serviceregistration(service->registeriface(pinginterface)),
      masterconn(NULL) { }

maybe<error>
storageslave::connect(clientio io, const registrationsecret &rs) {
    auto br(beaconclient(rs));
    if (br.isfailure()) return br.failure();
    auto sock(tcpsocket::connect(io, br.success().mastername));
    if (sock.isfailure()) return sock.failure();
    auto sr(rpcconnthread<storageslavectxt*>::spawn(
                service,
                rpcconnthread<storageslavectxt*>::nostartconn,
                rpcconnthread<storageslavectxt*>::noendconn,
                sock.success(),
                Nothing));
    if (sr.isfailure()) {
        sock.success().close();
        return sr.failure(); }
    auto &conn(sr.success()->conn);
    auto snr(conn.allocsequencenr());
    auto hellores(conn.call(
                      io,
                      wireproto::req_message(proto::HELLO::tag, snr)
                      .addparam(proto::HELLO::req::version, 1u)
                      .addparam(proto::HELLO::req::nonce, br.success().nonce)
                      .addparam(proto::HELLO::req::slavename,
                                br.success().slavename)
                      .addparam(proto::HELLO::req::digest,
                                digest("B" +
                                       fields::mk(br.success().nonce) +
                                       fields::mk(br.success().secret)))));
    conn.putsequencenr(snr);
    if (hellores.isfailure()) {
        delete sr.success();
        return hellores.failure(); }
    logmsg(loglevel::notice,
           "connected to master at " + fields::mk(br.success().mastername));

    masterconn = sr.success();
    return Nothing;
}

orerror<storageslave *>
storageslave::build(clientio io,
                    const registrationsecret &rs,
                    controlserver *cs)
{
    auto work(new storageslave(cs));
    auto err(work->connect(io, rs));
    if (err.isjust()) {
        delete work;
        return err.just();
    } else {
        return work;
    }
}

void
storageslave::destroy() const
{
    this->service->destroy();
    delete this->masterconn;
    delete this;
}

messageresult
storageslave::statusiface::message(const wireproto::rx_message &,
                                   controlconn *) {
    return error::unimplemented; }

template class rpcconnthread<storageslavectxt *>;
template class rpcinterface<storageslavectxt *>;
template class rpcregistration<storageslavectxt *>;
template class rpcservice<storageslavectxt *>;
template class list<rpcinterface<storageslavectxt *> *>;
template class list<rpcregistration<storageslavectxt *> *>;
