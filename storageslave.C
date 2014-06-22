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
#include "timedelta.H"
#include "timestamp.H"
#include "udpsocket.H"
#include "wireproto.H"

storageslave::storageslave(controlserver *cs)
    : statusinterface(this),
      controlregistration(
          cs->service->registeriface(statusinterface)) { }

maybe<error>
storageslave::connect(const registrationsecret &rs) {
    auto br(beaconclient(rs));
    if (br.isfailure()) return br.failure();
    auto cr(rpcconn::connectmaster(br.success()));
    if (cr.isfailure()) return cr.failure();
    masterconn = cr.success();
    return Nothing;
}

orerror<storageslave *>
storageslave::build(const registrationsecret &rs,
                    controlserver *cs)
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
storageslave::statusiface::message(const wireproto::rx_message &,
                                   controlconn *,
                                   buffer &)
{
    return error::unimplemented;
}
