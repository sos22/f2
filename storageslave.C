#include "storageslave.H"

#include "buffer.H"
#include "controlserver.H"
#include "fields.H"
#include "logging.H"
#include "nonce.H"
#include "orerror.H"
#include "peername.H"
#include "registrationsecret.H"
#include "udpsocket.H"
#include "wireproto.H"

storageslave::storageslave(const controlserver &cs)
    : statusinterface(this),
      controlregistration(
          cs.registeriface(statusinterface))
{
}

maybe<error>
storageslave::connect(const registrationsecret &) {
    auto sock(udpsocket::client());
    if (sock.isfailure()) return sock.failure();
    auto slavename(sock.success().localname());
    auto n(nonce::mk());
    buffer buf;
    {   auto serialiseres(wireproto::tx_message(proto::HAIL::tag)
                          .addparam(proto::HAIL::req::version, 1u)
                          .addparam(proto::HAIL::req::slavename, slavename)
                          .addparam(proto::HAIL::req::nonce, n)
                          .serialise(buf));
        if (serialiseres.isjust()) return serialiseres.just(); }
    auto sendres(sock.success().send(
                     buf, peername::udpbroadcast(peername::port(9009))));
    if (sendres.isjust()) return sendres.just();
    if (!buf.empty()) {
        logmsg(loglevel::failure, fields::mk("HAIL message truncated"));
        return error::truncated; }
    
    return error::unimplemented; }

orerror<storageslave *>
storageslave::build(const registrationsecret &rs,
                    const controlserver &cs)
{
    auto work(new storageslave(cs));
    auto err(work->connect(rs));
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
    delete this;
}

maybe<error>
storageslave::statusiface::controlmessage(const wireproto::rx_message &,
                                          buffer &)
{
    return error::unimplemented;
}
