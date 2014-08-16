#include "beaconclient.H"

#include "buffer.H"
#include "logging.H"
#include "either.H"
#include "fields.H"
#include "mastersecret.H"
#include "maybe.H"
#include "nonce.H"
#include "orerror.H"
#include "pair.H"
#include "parsers.H"
#include "peername.H"
#include "proto.H"
#include "registrationsecret.H"
#include "test.H"
#include "timedelta.H"
#include "udpsocket.H"

#include "either.tmpl"
#include "parsers.tmpl"
#include "test.tmpl"

/* You'd think that this would be the perfect place to use a template,
   and you'd be right, except that I can't figure out the syntax.  Do
   it by hand instead. */
namespace parsers {
template <> const parser<maybe<int> > &
_maybe(const parser<int> &inner) {
    return strmatcher("Nothing", maybe<int>(Nothing)) ||
        (("<" + inner + ">").map < maybe<int> > (
            [] (const int &what) {
                return maybe<int>(what); })); }
}

orerror<beaconresult>
beaconclient(clientio io, const beaconclientconfig &config)
{
    auto _sock(udpsocket::client());
    if (_sock.isfailure()) return _sock.failure();
    auto &sock(_sock.success());

    int counter;
    counter = 0;
    while (config.retrylimit_ == Nothing ||
           counter < config.retrylimit_.just()) {
        auto n(nonce::mk());
        counter++;
        logmsg(loglevel::info, fields::mk("send a HAIL"));
        buffer outbuf;
        wireproto::tx_message(proto::HAIL::tag)
            .addparam(proto::HAIL::req::version, 1u)
            .addparam(proto::HAIL::req::nonce, n)
            .serialise(outbuf);
        tests::beaconclientreadytosend.trigger(
            pair<fd_t, buffer *>(sock.asfd(), &outbuf));
        {   auto sendres(sock.send(
                    outbuf,
                    peername::udpbroadcast(config.port_)));
            if (!outbuf.empty()) {
                logmsg(loglevel::failure, fields::mk("HAIL message truncated"));
                sock.close();
                return error::truncated; } }
        
        /* Wait up to a second for a response before sending another
         * HAIL. */
        auto respdeadline(timestamp::now() + config.retryinterval_);
        while (1) {
            buffer inbuf;
            tests::beaconclientreceiving.trigger(mkpair(sock,n));
            auto recvres(sock.receive(io, inbuf, respdeadline));
            if (recvres.isfailure()) {
                if (recvres.failure() == error::timeout) {
/**/                break; /* Send another HAIL */
                } else {
                    sock.close();
                    return recvres.failure(); } }
            auto &rxfrom(recvres.success());
            auto deserialiseres(wireproto::rx_message::fetch(inbuf));
            if (deserialiseres.isfailure()) {
                logmsg(loglevel::failure,
                       "failed to decode HAIL response from " +
                       fields::mk(rxfrom));
                continue; }
            auto &rxmsg(deserialiseres.success());
            auto respversion(rxmsg.getparam(proto::HAIL::resp::version));
            auto respmastername(rxmsg.getparam(proto::HAIL::resp::mastername));
            auto respslavename(rxmsg.getparam(proto::HAIL::resp::slavename));
            auto respnonce(rxmsg.getparam(proto::HAIL::resp::nonce));
            auto respdigest(rxmsg.getparam(proto::HAIL::resp::digest));
            if (!respversion || !respmastername || !respslavename ||
                !respnonce || !respdigest) {
                logmsg(loglevel::failure,
                       "bad HAIL response from " + fields::mk(rxfrom) +
                       ": missing parameter");
                continue; }
            if (respversion.just() != 1) {
                logmsg(loglevel::failure,
                       "HAIL from " + fields::mk(rxfrom) +
                       " used version " + fields::mk(respversion.just()) +
                       ", but we only support version 1");
                continue; }
            if (respdigest.just() !=
                digest("A" +
                       fields::mk(respmastername.just()) +
                       fields::mk(n) +
                       fields::mk(config.rs_))) {
                logmsg(loglevel::failure,
                       "HAIL from " + fields::mk(rxfrom) +
                       " had a bad digest");
                continue; }
            
            logmsg(loglevel::notice,
                   "Received a good HAIL response from " +
                   fields::mk(respmastername.just()));
            
            sock.close();
            return beaconresult(respnonce.just(),
                                respslavename.just(),
                                respmastername.just(),
                                config.rs_); } }
    return error::timeout; }

beaconresult::beaconresult(const masternonce &_nonce,
                           const peername &_connectingname,
                           const peername &_mastername,
                           const registrationsecret &_secret)
    : nonce(_nonce), connectingname(_connectingname),
      mastername(_mastername), secret(_secret) {}

const fields::field &
fields::mk(const beaconclientconfig &bc) {
    return "<beaconclientconfig: rs=" + mk(bc.rs_) +
        " retryinterval=" + mk(bc.retryinterval_) +
        " retrylimit=" + mk(bc.retrylimit_) +
        " port=" + mk(bc.port_) + ">"; }

beaconclientconfig::beaconclientconfig(const quickcheck &q)
    : rs_(q),
      retryinterval_(q),
      retrylimit_(q),
      port_(q) {}

bool
beaconclientconfig::operator==(const beaconclientconfig &o) const {
    return rs_ == o.rs_ &&
        retryinterval_ == o.retryinterval_ &&
        retrylimit_ == o.retrylimit_ &&
        port_ == o.port_; }

const parser<beaconclientconfig> &
parsers::_beaconclientconfig() {
    return ("<beaconclientconfig: rs=" + _registrationsecret() +
            ~(" retryinterval=" + _timedelta()) +
            ~(" retrylimit=" + _maybe(intparser<int>())) +
            ~(" port=" + _peernameport()) +
            ">")
        .map<beaconclientconfig>(
            [] (const pair<pair<pair<registrationsecret,
                                     maybe<timedelta> >,
                                maybe<maybe<int> > >,
                           maybe<peername::port> > &x) {
                return beaconclientconfig(
                    x.first().first().first(),
                    x.first().first().second().dflt(timedelta::seconds(1)),
                    x.first().second().dflt(Nothing),
                    x.second().dflt(peername::port(9009))); }); }

namespace tests {
event< ::pair< ::fd_t, ::buffer *> > beaconclientreadytosend;
event< ::pair< ::udpsocket, ::nonce> > beaconclientreceiving;
}
