#include "rpcconn.H"

#include "fields.H"
#include "logging.H"
#include "proto.H"
#include "timedelta.H"
#include "wireproto.H"

#include "either.tmpl"
#include "list.tmpl"
#include "rpcconn.tmpl"
#include "wireproto.tmpl"

wireproto_wrapper_type(rpcconn::status_t)
namespace wireproto {
template <> maybe<rpcconn::status_t> deserialise(
    wireproto::bufslice &slice) {
    auto m(deserialise<rx_message>(slice));
    if (!m) return Nothing;
    else return rpcconn::status_t::fromcompound(m.just()); } }
void
rpcconn::status_t::addparam(wireproto::parameter<rpcconnstatus> tmpl,
                            wireproto::tx_message &txm) const {
    txm.addparam(wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
                 wireproto::tx_compoundparameter()
                 .addparam(proto::rpcconnstatus::outgoing, outgoing)
                 .addparam(proto::rpcconnstatus::fd, fd)
                 .addparam(proto::rpcconnstatus::sequencer, sequencer)
                 .addparam(proto::rpcconnstatus::pendingrx, pendingrx)
                 .addparam(proto::rpcconnstatus::peername, peername_)
                 .addparam(proto::rpcconnstatus::lastcontact,
                           lastcontact.as_timeval())); }
maybe<rpcconn::status_t>
rpcconn::status_t::getparam(wireproto::parameter<rpcconn::status_t> tmpl,
                            wireproto::rx_message const &msg) {
    auto packed(msg.getparam(
                    wireproto::parameter<wireproto::rx_message>(tmpl)));
    if (!packed) return Nothing;
    return rpcconn::status_t::fromcompound(packed.just()); }
maybe<rpcconn::status_t>
rpcconn::status_t::fromcompound(const wireproto::rx_message &p) {
#define doparam(name)                                   \
    auto name(p.getparam(proto::rpcconnstatus::name));  \
    if (!name) return Nothing;
    doparam(outgoing);
    doparam(fd);
    doparam(sequencer);
    doparam(peername);
    doparam(lastcontact);
#undef doparam
    list<wireproto::rx_messagestatus> pendingrx;
    auto r(p.fetch(proto::rpcconnstatus::pendingrx, pendingrx));
    if (r.isjust()) return Nothing;
    rpcconn::status_t res(outgoing.just(),
                          fd.just(),
                          sequencer.just(),
                          pendingrx,
                          peername.just(),
                          timestamp::fromtimeval(lastcontact.just()));
    pendingrx.flush();
    return res; }

const fields::field &
fields::mk(const rpcconn::status_t &o) {
    const field *acc = &("<outgoing: " + mk(o.outgoing) +
                         " fd:" + mk(o.fd) +
                         " sequencer:" + mk(o.sequencer) +
                         " peername:" + mk(o.peername_) +
                         " lastcontact:" +
                         mk(o.lastcontact.as_timeval()).asdate() +
                         " pendingrx:{");
    bool first = true;
    for (auto it(o.pendingrx.start()); !it.finished(); it.next()) {
        if (!first) acc = &(*acc + ",");
        first = false;
        acc = &(*acc + mk(*it)); }
    return *acc + "}>"; }

void
rpcconn::run(clientio io) {
    subscriber sub;
    subscription shutdownsub(sub, shutdown.pub);
    subscription grewsub(sub, outgoinggrew);
    iosubscription insub(io, sub, sock.poll(POLLIN));
    iosubscription outsub(io, sub, sock.poll(POLLOUT));
    buffer inbuffer;
    bool outarmed;

    /* Either Nothing if we're waiting to send a ping or the sequence
       of the last ping sent if there's one outstanding. */
    maybe<wireproto::sequencenr> pingsequence(Nothing);
    /* Either the time to send the next ping, if pingsequence == Nothing, or
       the deadline for receiving it, otherwise. */
    timestamp pingtime(timestamp::now() + timedelta::seconds(1));

    outarmed = true;
    subscriptionbase *ss;
    while (!shutdown.ready()) {
        ss = sub.wait(pingtime);
      gotss:
        if (ss == NULL) {
            /* Run the ping machine. */
            if (pingsequence.isjust()) {
                /* Failed to receive a ping in time.  This connection
                 * is dead. */
                logmsg(loglevel::failure,
                       fields::mk(peer_) + " failed to respond to ping");
                goto done;
            } else {
                /* Time to send a ping. */
                /* XXX this can take us over the outbuffer limit.
                   That's fine: it's pretty arbitrary what the actual
                   number is, and it's only ever over by a small,
                   fixed amount. */
                pingsequence = allocsequencenr();
                pingtime = timestamp::now() + timedelta::seconds(60);
                auto token(txlock.lock());
                wireproto::req_message(proto::PING::tag, pingsequence.just())
                    .serialise(outgoing);
                if (!outarmed) outsub.rearm();
                txlock.unlock(&token); }
        } else if (ss == &shutdownsub) {
            continue;
        } else if (ss == &grewsub) {
            if (!outarmed) outsub.rearm();
        } else if (ss == &insub) {
            auto rxres(inbuffer.receive(io, sock));
            if (rxres.isjust()) {
                rxres.just().warn("receiving from " + fields::mk(peer_));
                goto done; }
            insub.rearm();
            auto contacttoken(contactlock.lock());
            lastcontact = timestamp::now();
            contactlock.unlock(&contacttoken);
            if (pingsequence.isjust()) {
                pingtime = lastcontact + timedelta::seconds(60);
            } else {
                pingtime = lastcontact + timedelta::seconds(1); }
            while (!shutdown.ready()) {
                auto msg(wireproto::rx_message::fetch(inbuffer));
                if (msg.isfailure()) {
                    if (msg.failure() == error::underflowed) {
                        break;
                    } else {
                        msg.failure().warn(
                            "decoding message from " + fields::mk(peer_));
                        goto done; } }
                if (msg.success().isreply()) {
                    if (pingsequence.isjust() &&
                        msg.success().sequence() ==
                            pingsequence.just().reply()) {
                        logmsg(loglevel::debug,
                               "ping response from " + fields::mk(peer_));
                        pingsequence = Nothing;
                        pingtime = timestamp::now() + timedelta::seconds(1);
                    } else {
                        auto token(rxlock.lock());
                        pendingrx.pushtail(msg.success().steal());
                        pendingrxextended.publish();
                        rxlock.unlock(&token); }
                    continue;
                }

                auto res(message(msg.success()));
                if (!res.isreply() && !res.isfailure()) {
                    /* No response to this message */
                    continue; }

                /* Sending a response has to wait for buffer space,
                   blocking RX while it's doing so, because otherwise
                   we don't get the right backpressure.  We don't want
                   to block other TX, though, to avoid silly
                   deadlocks. */
                auto txtoken(txlock.lock());
                subscriber shutdownonlysub;
                subscription soss(shutdownonlysub, shutdown.pub);
                while (!shutdown.ready() &&
                       outgoing.avail() >= MAX_OUTGOING_BYTES) {
                    auto txres(outgoing.send(io, sock, sub));
                    outgoingshrunk.publish();
                    txlock.unlock(&txtoken);
                    if (txres.isfailure()) {
                        txres.failure().warn(
                            "clearing space for a reply to " +
                            fields::mk(peer_));
                        if (res.isreply()) delete res.reply();
                        return; }
                    txtoken = txlock.lock(); }
                if (res.isfailure()) {
                    wireproto::err_resp_message(
                        msg.success(), res.failure())
                        .serialise(outgoing);
                } else if (res.isreply()) {
                    res.reply()->serialise(outgoing);
                    delete res.reply(); }
                if (!outarmed) {
                    outsub.rearm();
                    outarmed = true; }
                txlock.unlock(&txtoken); }
        } else {
            assert(ss == &outsub);
            outarmed = false;
            auto token(txlock.lock());
            if (!outgoing.empty()) {
                auto txres(outgoing.send(io, sock, sub));
                if (!outgoing.empty()) {
                    outarmed = true;
                    outsub.rearm(); }
                outgoingshrunk.publish();
                txlock.unlock(&token);
                if (txres.isfailure()) {
                    txres.failure().warn("sending to " + fields::mk(peer_));
                    goto done; }
                if (txres.success() != NULL) {
                    ss = txres.success();
                    goto gotss; }
            } else {
                /* Don't rearm: the outgoing queue is empty, so we
                 * don't need to. */
                txlock.unlock(&token); } } }
  done:
    endconn(io); }

rpcconn::rpcconn(
    socket_t _sock,
    const peername &_peer)
    : thr(NULL),
      shutdown(),
      sock(_sock),
      txlock(),
      outgoing(),
      outgoingshrunk(),
      outgoinggrew(),
      sequencelock(),
      sequencer(),
      rxlock(),
      pendingrx(),
      pendingrxextended(),
      contactlock(),
      lastcontact(timestamp::now()),
      peer_(_peer) {}

messageresult
rpcconn::message(const wireproto::rx_message &msg) {
    if (msg.tag() == proto::PING::tag) {
        static int cntr;
        return &(*new wireproto::resp_message(msg))
            .addparam(proto::PING::resp::cntr, cntr++);
    } else {
        return error::unimplemented; } }
wireproto::sequencenr
rpcconn::allocsequencenr() {
    auto token(sequencelock.lock());
    auto res(sequencer.get());
    sequencelock.unlock(&token);
    return res; }

void
rpcconn::putsequencenr(wireproto::sequencenr snr) {
    auto token(sequencelock.lock());
    sequencer.put(snr);
    sequencelock.unlock(&token); }

peername
rpcconn::peer() const {
    return peer_; }

rpcconn::sendres
rpcconn::send(
    clientio,
    const wireproto::tx_message &msg,
    subscriber &sub,
    maybe<timestamp> deadline) {
    auto txtoken(txlock.lock());
    if (outgoing.avail() >= MAX_OUTGOING_BYTES) {
        subscription moretx(sub, outgoingshrunk);
        while (outgoing.avail() >= MAX_OUTGOING_BYTES) {
            txlock.unlock(&txtoken);
            auto res = sub.wait(deadline);
            if (res == NULL) return error::timeout;
            if (res != &moretx) return res;
            txtoken = txlock.lock(); } }
    auto res(msg.serialise(outgoing));
    outgoinggrew.publish();
    txlock.unlock(&txtoken);
    if (res.isjust()) return res.just();
    else return sendres(); }

maybe<error>
rpcconn::send(
    clientio io,
    const wireproto::tx_message &msg,
    maybe<timestamp> deadline) {
    subscriber sub;
    auto res(send(io, msg, sub, deadline));
    assert(!res.isnotified());
    if (res.isfailure()) return res.failure();
    assert(res.issuccess());
    return Nothing; }

rpcconn::callres
rpcconn::call(
    clientio io,
    const wireproto::req_message &msg,
    subscriber &sub,
    maybe<timestamp> deadline) {
    {   auto txres(send(io, msg, sub, deadline));
        if (txres.isfailure()) return txres.failure();
        if (txres.isnotified()) return txres.notified();
        assert(txres.issuccess()); }
    subscription morerx(sub, pendingrxextended);
    while (1) {
        {   auto rxtoken(rxlock.lock());
            for (auto it(pendingrx.start()); !it.finished(); it.next()) {
                if ((*it)->sequence() == msg.sequence.reply()) {
                    auto res(*it);
                    it.remove();
                    rxlock.unlock(&rxtoken);
                    return res; } }
            rxlock.unlock(&rxtoken); }
        auto res(sub.wait(deadline));
        if (res == NULL) return error::timeout;
        if (res != &morerx) return res; } }


orerror<const wireproto::rx_message *>
rpcconn::call(
    clientio io,
    const wireproto::req_message &msg,
    maybe<timestamp> deadline) {
    subscriber sub;
    auto res(call(io, msg, sub, deadline));
    if (res.isfailure()) return res.failure();
    else if (res.isnotified()) abort();
    else return res.success(); }

void
rpcconn::teardown() {
    shutdown.set(true); }

maybe<rpcconn::deathtoken>
rpcconn::hasdied() const {
    if (thr->finished()) return deathtoken();
    else return Nothing; }

const publisher &
rpcconn::deathpublisher() const {
    return thr->pub; }

void
rpcconn::destroy(deathtoken) {
    assert(thr->finished());
    /* Can't actually block because we know the thread has already
       finished, so don't need a real clientio token. */
    thr->join(clientio::CLIENTIO);
    thr = NULL;
    while (!pendingrx.empty()) delete pendingrx.pophead();
    delete this; }

rpcconn::~rpcconn() {
    assert(thr == NULL);
    sock.close(); }

void
rpcconn::drain(clientio) {
    subscriber sub;
    subscription ss(sub, outgoingshrunk);
    auto token(txlock.lock());
    while (!outgoing.empty()) {
        txlock.unlock(&token);
        auto r(sub.wait());
        assert(r == &ss);
        token = txlock.lock(); }
    txlock.unlock(&token); }

void
rpcconn::destroy(clientio) {
    teardown();
    maybe<deathtoken> dt(Nothing);
    {   subscriber sub;
        subscription ss(sub, deathpublisher());
        while (1) {
            dt = hasdied();
            if (dt.isjust()) break;
            auto s = sub.wait();
            assert(s == &ss); } }
    destroy(dt.just()); }

rpcconn::status_t
rpcconn::status(maybe<mutex_t::token>) const {
    list<wireproto::rx_message::status_t> prx;
    auto tok(rxlock.lock());
    for (auto it(pendingrx.start()); !it.finished(); it.next()) {
        prx.pushtail((*it)->status()); }
    rxlock.unlock(&tok);
    return status_t(outgoing.status(),
                    sock.status(),
                    sequencer.status(),
                    prx,
                    peer_,
                    lastcontact);
}

const messageresult
messageresult::noreply;

template class either<subscriptionbase*, wireproto::rx_message const*>;
template bool list<rpcconnstatus>::empty() const;
template list<rpcconnstatus>::list();
template list<rpcconnstatus>::list(list<rpcconnstatus> const&);
template void list<rpcconnstatus>::flush();
template void list<rpcconnstatus>::pushtail(rpcconnstatus const&);
template list<rpcconnstatus>::~list();
template list<rpcconnstatus>::const_iter list<rpcconnstatus>::start() const;
template list<rpcconnstatus>::const_iter::const_iter(
    list<rpcconnstatus> const*, bool);
template void list<rpcconnstatus>::const_iter::next();
template bool list<rpcconnstatus>::const_iter::finished() const;
template const rpcconnstatus &list<rpcconnstatus>::const_iter::
    operator*() const;
template list<rpcconnstatus>::iter list<rpcconnstatus>::start();
template list<rpcconnstatus>::iter::iter(list<rpcconnstatus> *, bool);
template void list<rpcconnstatus>::iter::remove();
template bool list<rpcconnstatus>::iter::finished() const;
template wireproto::tx_message& wireproto::tx_message::addparam<rpcconnstatus>(
    wireproto::parameter<list<rpcconnstatus> >, list<rpcconnstatus> const&);
template maybe<error> wireproto::rx_message::fetch<rpcconnstatus>(
    wireproto::parameter<list<rpcconnstatus> >, list<rpcconnstatus>&) const;
template orerror<rpcconn*> rpcconn::fromsocket<rpcconn>(socket_t);
template orerror<rpcconn*> rpcconn::connect<rpcconn>(clientio, peername const&);
