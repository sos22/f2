#include "rpcconn.H"

#include "fields.H"
#include "logging.H"
#include "proto.H"
#include "slavename.H"
#include "timedelta.H"
#include "walltime.H"
#include "wireproto.H"

#include "either.tmpl"
#include "list.tmpl"
#include "rpcconn.tmpl"
#include "waitbox.tmpl"
#include "wireproto.tmpl"

wireproto_wrapper_type(rpcconn::status_t)
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
                 .addparam(proto::rpcconnstatus::lastcontact, lastcontact)); }
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
    if (r.isfailure()) return Nothing;
    rpcconn::status_t res(outgoing.just(),
                          fd.just(),
                          sequencer.just(),
                          pendingrx,
                          peername.just(),
                          lastcontact.just());
    pendingrx.flush();
    return res; }

const fields::field &
fields::mk(const rpcconn::status_t &o) {
    const field *acc = &("<outgoing: " + mk(o.outgoing) +
                         " fd:" + mk(o.fd) +
                         " sequencer:" + mk(o.sequencer) +
                         " peername:" + mk(o.peername_) +
                         " lastcontact:" + mk(o.lastcontact) +
                         " pendingrx:{");
    bool first = true;
    for (auto it(o.pendingrx.start()); !it.finished(); it.next()) {
        if (!first) acc = &(*acc + ",");
        first = false;
        acc = &(*acc + mk(*it)); }
    return *acc + "}>"; }

maybe<class slavename>
rpcconnauth::slavename() const {
    if (state == s_done) return ((done *)buf)->slave;
    else return Nothing; }

rpcconnauth
rpcconnauth::mkdone() {
    rpcconnauth r;
    r.state = s_done;
    new (r.buf) done(Nothing);
    return r; }

rpcconnauth
rpcconnauth::mkwaithello(
    const mastersecret &ms,
    const registrationsecret &rs) {
    rpcconnauth r;
    r.state = s_waithello;
    new (r.buf) waithello(ms, rs);
    return r; }

rpcconnauth::waithello::waithello(
    const mastersecret &_ms,
    const registrationsecret &_rs)
    : ms(_ms),
      rs(_rs) {}

rpcconnauth
rpcconnauth::mksendhelloslavea(
    const registrationsecret &rs) {
    rpcconnauth r;
    r.state = s_sendhelloslavea;
    new (r.buf) sendhelloslavea(rs);
    return r; }

rpcconnauth::sendhelloslavea::sendhelloslavea(
    const registrationsecret &_rs)
    : rs(_rs) {}

rpcconnauth
rpcconnauth::mkwaithelloslavea(
    const registrationsecret &rs,
    waitbox<orerror<void> > *wb) {
    rpcconnauth r;
    r.state = s_waithelloslavea;
    new (r.buf) waithelloslavea(rs, wb);
    return r; }

rpcconnauth::waithelloslavea::waithelloslavea(
    const registrationsecret &_rs,
    waitbox<orerror<void> > *_wb)
    : rs(_rs),
      wb(_wb) {}

rpcconnauth::waithelloslaveb::waithelloslaveb(
    const registrationsecret &_rs,
    const nonce &_n)
    : rs(_rs),
      n(_n) {}

rpcconnauth::rpcconnauth(const rpcconnauth &o)
    : state(o.state) {
    switch (state) {
    case s_preinit:
        abort();
    case s_done:
        new (buf) done(*(done *)o.buf);
        return;
    case s_waithello:
        new (buf) waithello(*(waithello *)o.buf);
        return;
    case s_sendhelloslavea:
        new (buf) sendhelloslavea(*(sendhelloslavea *)o.buf);
        return;
    case s_waithelloslavea:
        new (buf) waithelloslavea(*(waithelloslavea *)o.buf);
        return;
    case s_waithelloslaveb:
        new (buf) waithelloslaveb(*(waithelloslaveb *)o.buf);
        return;
    case s_waithelloslavec:
        new (buf) waithelloslavec(*(waithelloslavec *)o.buf);
        return; }
    if (!COVERAGE) abort(); }

rpcconnauth::~rpcconnauth() {
    switch (state) {
    case s_preinit:
        abort();
    case s_done:
        ((done *)buf)->~done();
        return;
    case s_waithello:
        ((waithello *)buf)->~waithello();
        return;
    case s_sendhelloslavea:
        ((sendhelloslavea *)buf)->~sendhelloslavea();
        return;
    case s_waithelloslavea:
        ((waithelloslavea *)buf)->~waithelloslavea();
        return;
    case s_waithelloslaveb:
        ((waithelloslaveb *)buf)->~waithelloslaveb();
        return;
    case s_waithelloslavec:
        ((waithelloslavec *)buf)->~waithelloslavec();
        return; }
    if (!COVERAGE) abort(); }

rpcconnauth::rpcconnauth()
    : state(s_preinit) {}

rpcconnauth::done::done(const maybe<class slavename> &o)
    : slave(o) {}

void
rpcconnauth::start(buffer &b) {
    if (state != s_sendhelloslavea) return;
    auto n(nonce::mk());
    wireproto::tx_message(proto::HELLOSLAVE::A::tag)
        .addparam(proto::HELLOSLAVE::A::nonce, n)
        .serialise(b);
    auto rs( ((sendhelloslavea *)buf)->rs );
    ((sendhelloslavea *)buf)->~sendhelloslavea();
    new (buf) waithelloslaveb(rs, n);
    state = s_waithelloslaveb; }

maybe<messageresult>
rpcconnauth::message(const wireproto::rx_message &rxm,
                     const peername &peer) {
    switch (state) {
    case s_preinit: abort();
    case s_done: return Nothing;
    case s_waithello: {
        waithello *s = (waithello *)buf;
        if (rxm.tag() != proto::HELLO::tag) {
            logmsg(loglevel::failure,
                   "received message tag " +
                   fields::mk(rxm.tag()) +
                   " from " +
                   fields::mk(peer) +
                   " without a HELLO");
            return messageresult(error::unrecognisedmessage); }
        auto digest(rxm.getparam(proto::HELLO::req::digest));
        auto nonce(rxm.getparam(proto::HELLO::req::nonce));
        auto peername(rxm.getparam(proto::HELLO::req::peername));
        auto _slavename(rxm.getparam(proto::HELLO::req::slavename));
        auto version(rxm.getparam(proto::HELLO::req::version));
        if (!digest || !nonce || !peername || !_slavename || !version) {
            return messageresult(error::missingparameter); }
        logmsg(loglevel::verbose,
               "HELLO version " + fields::mk(version) +
               "nonce " + fields::mk(nonce) +
               "peername " + fields::mk(peername) +
               "digest " + fields::mk(digest));
        if (version.just() != 1) return messageresult(error::badversion);
        if (!s->ms.noncevalid(nonce.just(), peername.just())) {
            logmsg(loglevel::notice,
                   "HELLO with invalid nonce from " + fields::mk(peer));
            return messageresult(error::authenticationfailed); }
        if (!peername.just().samehost(peer)) {
            logmsg(loglevel::notice,
                   "HELLO with bad host (" + fields::mk(peername.just()) +
                   ", expected host " + fields::mk(peer) +")");
            return messageresult(error::authenticationfailed); }
        if (digest.just() != ::digest("B" +
                                      fields::mk(nonce.just()) +
                                      fields::mk(s->rs))) {
            logmsg(loglevel::notice,
                   "HELLO with invalid digest from " + fields::mk(peer));
            return messageresult(error::authenticationfailed); }
        logmsg(loglevel::notice, "Valid HELLO from " + fields::mk(peer));
        s->~waithello();
        state = s_done;
        new (buf) done(_slavename);
        return messageresult(new wireproto::resp_message(rxm)); }
    case s_sendhelloslavea:
        /* Shouldn't get here; should have sent the HELLOSLAVE::A
           before checking for messages from the other side. */
        abort();
    case s_waithelloslavea: {
        auto s = (waithelloslavea *)buf;
        auto wb(s->wb);
        if (rxm.tag() != proto::HELLOSLAVE::A::tag) {
            logmsg(loglevel::failure,
                   "received message " +
                   fields::mk(&rxm) +
                   " from " +
                   fields::mk(peer) +
                   "; expected HELLOSLAVE::A");
            wb->set(error::unrecognisedmessage);
            return messageresult(error::unrecognisedmessage); }
        logmsg(loglevel::info, fields::mk("got a HELLOSLAVE A"));
        auto nonce(rxm.getparam(proto::HELLOSLAVE::A::nonce));
        if (!nonce) {
            wb->set(error::missingparameter);
            return messageresult(error::missingparameter); }
        auto txm(new wireproto::tx_message(proto::HELLOSLAVE::B::tag));
        txm->addparam(proto::HELLOSLAVE::B::digest,
                      ::digest("C" +
                               fields::mk(s->rs) +
                               fields::mk(nonce.just())));
        state = s_waithelloslavec;
        s->~waithelloslavea();
        new (buf) waithelloslavec(wb);
        return messageresult(txm); }
    case s_waithelloslaveb: {
        auto s = (waithelloslaveb *)buf;
        if (rxm.tag() != proto::HELLOSLAVE::B::tag) {
            logmsg(loglevel::failure,
                   "received message " +
                   fields::mk(&rxm) +
                   " from " +
                   fields::mk(peer) +
                   "; expected HELLOSLAVE::B");
            return messageresult(error::unrecognisedmessage); }
        logmsg(loglevel::info, fields::mk("got a HELLOSLAVE B"));
        auto digest(rxm.getparam(proto::HELLOSLAVE::B::digest));
        if (!digest) return messageresult(error::missingparameter);
        if (digest.just() != ::digest("C" +
                                      fields::mk(s->rs) +
                                      fields::mk(s->n))) {
            logmsg(loglevel::notice,
                   "HELLOSLAVE::B with invalid digest from " +
                   fields::mk(peer));
            return messageresult(error::authenticationfailed); }
        state = s_done;
        s->~waithelloslaveb();
        new (buf) done(Nothing);
        return messageresult(
            new wireproto::tx_message(proto::HELLOSLAVE::C::tag)); }
    case s_waithelloslavec: {
        auto s = (waithelloslavec *)buf;
        auto wb(s->wb);
        if (rxm.tag() == proto::PING::tag) {
            return messageresult::noreply; }
        if (rxm.tag() != proto::HELLOSLAVE::C::tag) {
            logmsg(loglevel::failure,
                   "received message " +
                   fields::mk(&rxm) +
                   " from " +
                   fields::mk(peer) +
                   "; expected HELLOSLAVE::C");
            wb->set(error::unrecognisedmessage);
            return messageresult(error::unrecognisedmessage); }
        logmsg(loglevel::info, fields::mk("got a HELLOSLAVE C"));
        {   auto err(rxm.getparam(wireproto::err_parameter));
            if (err.isjust()) wb->set(err.just());
            else wb->set(Success); }
        s->~waithelloslavec();
        state = s_done;
        new (buf) done(Nothing);
        return messageresult::noreply; } }
    if (!COVERAGE) abort();
    return Nothing; }

bool
rpcconn::queuereply(clientio io, wireproto::tx_message &msg) {
    /* Sending a response has to wait for buffer space, blocking RX
       while it's doing so, because otherwise we don't get the right
       backpressure.  We don't want to block other TX, though, to
       avoid silly deadlocks. */
    size_t msgsz(msg.serialised_size());
    assert(msgsz <= MAX_OUTGOING_BYTES);
    auto txtoken(txlock.lock());
    if (outgoing.avail() + msgsz > MAX_OUTGOING_BYTES) {
        subscriber reducedsub;
        subscription soss(reducedsub, shutdown.pub);
        while (!shutdown.ready() &&
               outgoing.avail() + msgsz > MAX_OUTGOING_BYTES) {
            auto txres(outgoing.send(io, sock, reducedsub));
            outgoingshrunk.publish();
            if (shutdown.ready()) break;
            txlock.unlock(&txtoken);
            if (txres.isfailure()) {
                txres.failure().warn(
                    "clearing space for a reply to " + fields::mk(peer_));
                return true; }
            reducedsub.wait(io);
            txtoken = txlock.lock(); } }
    if (shutdown.ready()) {
        txlock.unlock(&txtoken);
        return true; }
    msg.serialise(outgoing);
    txlock.unlock(&txtoken);
    return false; }

void
rpcconn::run(clientio io) {
    subscriber sub;
    subscription shutdownsub(sub, shutdown.pub);
    subscription grewsub(sub, outgoinggrew);
    iosubscription insub(sub, sock.poll(POLLIN));
    iosubscription outsub(sub, sock.poll(POLLOUT));
    buffer inbuffer;
    bool outarmed;

    /* Either Nothing if we're waiting to send a ping or the sequence
       of the last ping sent if there's one outstanding. */
    maybe<wireproto::sequencenr> pingsequence(Nothing);
    /* Either the time to send the next ping, if pingsequence == Nothing, or
       the deadline for receiving it, otherwise. */
    timestamp pingtime(timestamp::now() + timedelta::seconds(1));

    auto authtok(authlock.lock());
    auth(authtok).start(outgoing);
    authlock.unlock(&authtok);

    outarmed = true;
    subscriptionbase *ss;
    while (!shutdown.ready()) {
        ss = sub.wait(io, pingtime);
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
                /* This can take us over the outbuffer limit.  That's
                   fine: it's pretty arbitrary what the actual number
                   is, and we're only ever over by a small, fixed
                   amount. */
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
            if (rxres.isfailure()) {
                rxres.warn("receiving from " + fields::mk(peer_));
                goto done; }
            insub.rearm();
            auto contacttoken(contactlock.lock());
            lastcontact_monotone = timestamp::now();
            lastcontact_wall = walltime::now();
            contactlock.unlock(&contacttoken);
            if (pingsequence.isjust()) {
                pingtime = lastcontact_monotone + timedelta::seconds(60);
            } else {
                pingtime = lastcontact_monotone + timedelta::seconds(1); }
            while (!shutdown.ready()) {
                auto msg(wireproto::rx_message::fetch(inbuffer));
                if (msg.isfailure()) {
                    if (msg.failure() == error::underflowed) {
                        break;
                    } else {
                        msg.failure().warn(
                            "decoding message from " + fields::mk(peer_));
                        goto done; } }
                authtok = authlock.lock();
                auto authres(auth(authtok).message(msg.success(), peer_));
                authlock.unlock(&authtok);
                if (authres != Nothing) {
                    if (authres.just().isfailure()) {
                        /* Authentication protocol rejected the
                         * connection.  Tear it down. */
                        goto done; }
                    if (authres.just().isreply()) {
                        bool out = queuereply(io, *authres.just().reply());
                        delete authres.just().reply();
                        if (out) goto done;
                        if (!outarmed) {
                            outsub.rearm();
                            outarmed = true; } }
                    continue; }
                if (msg.success().isreply()) {
                    if (msg.success().tag() == proto::PING::tag &&
                        pingsequence.isjust() &&
                        msg.success().sequence() ==
                        pingsequence.just().reply()) {
                        logmsg(loglevel::debug,
                               "ping response from " + fields::mk(peer_));
                        pingsequence = Nothing;
                        pingtime = timestamp::now() + timedelta::seconds(1);
                    } else {
                        /* XXX This will leak, with no visible
                         * warning, if we receive a reply we weren't
                         * expecting. */
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

                bool out;
                if (res.isreply()) {
                    out = queuereply(io, *res.reply());
                    delete res.reply();
                } else {
                    wireproto::err_resp_message m(msg.success(), res.failure());
                    out = queuereply(io, m); }
                if (out) goto done;
                if (!outarmed) {
                    outsub.rearm();
                    outarmed = true; } }
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
    thread::constoken tok,
    socket_t _sock,
    const rpcconnauth &__auth,
    const peername &_peer)
    : thread(tok),
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
      lastcontact_monotone(timestamp::now()),
      lastcontact_wall(walltime::now()),
      peer_(_peer),
      _auth(__auth) {}

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

peername
rpcconn::peer() const {
    return peer_; }

maybe<class slavename>
rpcconn::slavename() const {
    auto token(authlock.lock());
    auto res(auth(token).slavename());
    authlock.unlock(&token);
    return res; }

rpcconn::sendres
rpcconn::send(
    clientio io,
    const wireproto::tx_message &msg,
    subscriber &sub,
    maybe<timestamp> deadline) {
    auto txtoken(txlock.lock());
    if (outgoing.avail() >= MAX_OUTGOING_BYTES) {
        subscription moretx(sub, outgoingshrunk);
        while (outgoing.avail() >= MAX_OUTGOING_BYTES) {
            txlock.unlock(&txtoken);
            auto res = sub.wait(io, deadline);
            if (res == NULL) return error::timeout;
            if (res != &moretx) return res;
            txtoken = txlock.lock(); } }
    msg.serialise(outgoing);
    outgoinggrew.publish();
    txlock.unlock(&txtoken);
    return sendres(); }

orerror<void>
rpcconn::send(
    clientio io,
    const wireproto::tx_message &msg,
    maybe<timestamp> deadline) {
    subscriber sub;
    auto res(send(io, msg, sub, deadline));
    assert(!res.isnotified());
    if (res.isfailure()) return res.failure();
    else return Success; }

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
                    auto err(res->getparam(wireproto::err_parameter));
                    if (err.isjust()) {
                        delete res;
                        return err.just();
                    } else {
                        return res; } } }
            rxlock.unlock(&rxtoken); }
        auto res(sub.wait(io, deadline));
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
    return thread::hasdied()
        .map<deathtoken>([] (thread::deathtoken t) {
                return deathtoken(t); }); }

rpcconn::~rpcconn() {
    sock.close(); }

void
rpcconn::drain(clientio io) {
    subscriber sub;
    subscription ss(sub, outgoingshrunk);
    auto token(txlock.lock());
    while (!outgoing.empty()) {
        txlock.unlock(&token);
        auto r(sub.wait(io));
        assert(r == &ss);
        token = txlock.lock(); }
    txlock.unlock(&token); }

void
rpcconn::destroy(clientio io) {
    drain(io);
    teardown();
    join(io); }

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
                    lastcontact_wall);
}

const messageresult
messageresult::noreply;
