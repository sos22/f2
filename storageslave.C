#include "storageslave.H"

#include "buffer.H"
#include "controlserver.H"
#include "fields.H"
#include "logging.H"
#include "mastersecret.H"
#include "nonce.H"
#include "orerror.H"
#include "peername.H"
#include "registrationsecret.H"
#include "timedelta.H"
#include "timestamp.H"
#include "udpsocket.H"
#include "wireproto.H"

storageslave::storageslave(const controlserver &cs)
    : statusinterface(this),
      controlregistration(
          cs.registeriface(statusinterface)) { }

maybe<error>
storageslave::connect(const registrationsecret &rs) {
    auto sock(udpsocket::client());
    if (sock.isfailure()) return sock.failure();
    auto n(nonce::mk());
    
    /* We keep trying to HAIL the master forever.  There's not much
       point in giving up early here; it's not like we can do anything
       useful if we can't contact the master. */
    while (true) {
        logmsg(loglevel::info, fields::mk("send a HAIL"));
        buffer outbuf;
        {   auto serialiseres(wireproto::tx_message(proto::HAIL::tag)
                              .addparam(proto::HAIL::req::version, 1u)
                              .addparam(proto::HAIL::req::nonce, n)
                              .serialise(outbuf));
            if (serialiseres.isjust()) return serialiseres.just(); }
        {   auto sendres(sock.success().send(
                    outbuf,
                    peername::udpbroadcast(peername::port(9009))));
            if (!outbuf.empty()) {
                logmsg(loglevel::failure, fields::mk("HAIL message truncated"));
                return error::truncated; } }
        
        /* Wait up to a second for a response before sending another
         * HAIL. */
        auto respdeadline(timestamp::now() + timedelta::seconds(120));
        while (1) {
            buffer inbuf;
            auto recvres(sock.success().receive(inbuf, respdeadline));
            if (recvres.isfailure()) {
                if (recvres.failure() == error::timeout) {
                    break;
                } else {
                    return recvres.failure(); } }
            auto &rxfrom(recvres.success());
            auto deserialiseres(wireproto::rx_message::fetch(inbuf));
            if (deserialiseres.isfailure()) {
                logmsg(loglevel::failure,
                       "failed to decode HAIL response from " +
                       fields::mk(rxfrom));
                continue; }
            auto *rxmsg(deserialiseres.success());
            auto respversion(rxmsg->getparam(proto::HAIL::resp::version));
            auto respmastername(rxmsg->getparam(proto::HAIL::resp::mastername));
            auto respnonce(rxmsg->getparam(proto::HAIL::resp::nonce));
            auto respdigest(rxmsg->getparam(proto::HAIL::resp::digest));
            if (respversion == Nothing || respmastername == Nothing ||
                respnonce == Nothing || respdigest == Nothing) {
                logmsg(loglevel::failure,
                       "bad HAIL response from " + fields::mk(rxfrom) +
                       ": missing parameter");
                rxmsg->finish();
                continue; }
            if (respversion.just() != 1) {
                logmsg(loglevel::failure,
                       "HAIL from " + fields::mk(rxfrom) +
                       " used version " + fields::mk(respversion.just()) +
                       ", but we only support version 1");
                rxmsg->finish();
                continue; }
            if (respdigest.just() !=
                digest("A" +
                       fields::mk(respmastername.just()) +
                       fields::mk(n) +
                       fields::mk(rs))) {
                logmsg(loglevel::failure,
                       "HAIL from " + fields::mk(rxfrom) +
                       " had a bad digest");
                rxmsg->finish();
                continue; }
            
            logmsg(loglevel::notice,
                   "Received a good HAIL response from " +
                   fields::mk(respmastername.just()));
            rxmsg->finish();
            break; } }
    
    return error::unimplemented;
}

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
