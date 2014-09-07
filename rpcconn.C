#include "rpcconn.H"

#include "fields.H"
#include "logging.H"
#include "parsers.H"
#include "proto.H"
#include "slavename.H"
#include "test.H"
#include "timedelta.H"
#include "walltime.H"
#include "wireproto.H"

#include "either.tmpl"
#include "list.tmpl"
#include "maybe.tmpl"
#include "parsers.tmpl"
#include "rpcconn.tmpl"
#include "test.tmpl"
#include "waitbox.tmpl"
#include "wireproto.tmpl"

tests::event<void> tests::__rpcconn::calldonetx;
tests::event<void> tests::__rpcconn::receivedreply;
tests::event<wireproto::tx_message **> tests::__rpcconn::sendinghelloslavec;
tests::event<rpcconn *> tests::__rpcconn::threadawoken;
tests::event<void> tests::__rpcconn::replystopped;

namespace proto {
namespace rpcconnconfig {
static const wireproto::parameter<unsigned> maxoutgoingbytes(1);
static const wireproto::parameter<timedelta> pinginterval(2);
static const wireproto::parameter<timedelta> pingdeadline(3);
static const wireproto::parameter<ratelimiterconfig> pinglimit(4); }
namespace rpcconnstatus {
static const parameter<class ::bufferstatus> outgoing(1);
static const parameter<class ::fd_tstatus> fd(2);
static const parameter<wireproto::sequencerstatus> sequencer(3);
static const parameter<list<wireproto::rx_messagestatus> > pendingrx(4);
static const parameter<class ::peername> peername(5);
static const parameter<class ::walltime> lastcontact(6);
static const parameter<class ::slavename> otherend(7);
static const parameter<class ::actortype> otherendtype(8);
static const parameter<class ::rpcconnconfig> config(9); } }

rpcconnconfig::rpcconnconfig(unsigned _maxoutgoingbytes,
                             timedelta _pinginterval,
                             timedelta _pingdeadline,
                             const ratelimiterconfig &_pinglimiter)
    : maxoutgoingbytes(_maxoutgoingbytes),
      pinginterval(_pinginterval),
      pingdeadline(_pingdeadline),
      pinglimit(_pinglimiter) {}
rpcconnconfig::rpcconnconfig(quickcheck q)
    : maxoutgoingbytes(q),
      pinginterval(q),
      pingdeadline(q),
      pinglimit(q) {}
wireproto_wrapper_type(rpcconnconfig)
void
rpcconnconfig::addparam(wireproto::parameter<rpcconnconfig> tmpl,
                        wireproto::tx_message &txm) const {
    txm.addparam(wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
                 wireproto::tx_compoundparameter()
                 .addparam(proto::rpcconnconfig::maxoutgoingbytes,
                           maxoutgoingbytes)
                 .addparam(proto::rpcconnconfig::pinginterval, pinginterval)
                 .addparam(proto::rpcconnconfig::pingdeadline, pingdeadline)
                 .addparam(proto::rpcconnconfig::pinglimit, pinglimit)); }
maybe<rpcconnconfig>
rpcconnconfig::fromcompound(const wireproto::rx_message &p) {
#define doparam(name)                                   \
    auto name(p.getparam(proto::rpcconnconfig::name));  \
    if (!name) return Nothing;
    doparam(maxoutgoingbytes);
    doparam(pinginterval);
    doparam(pingdeadline);
    doparam(pinglimit);
#undef doparam
    return rpcconnconfig(maxoutgoingbytes.just(),
                         pinginterval.just(),
                         pingdeadline.just(),
                         pinglimit.just()); }
bool
rpcconnconfig::operator==(const rpcconnconfig &o) const {
    return maxoutgoingbytes == o.maxoutgoingbytes &&
        pinginterval == o.pinginterval &&
        pingdeadline == o.pingdeadline &&
        pinglimit == o.pinglimit; }
const fields::field &
fields::mk(const rpcconnconfig &c) {
    return "<rpcconnconfig:"
        " maxoutgoingbytes:" + fields::mk(c.maxoutgoingbytes) +
        " pinginterval:" + fields::mk(c.pinginterval) +
        " pingdeadline:" + fields::mk(c.pingdeadline) +
        " pinglimit:" + fields::mk(c.pinglimit) +
        ">"; }
const parser<rpcconnconfig> &
parsers::_rpcconnconfig() {
    return ("<rpcconnconfig:" +
            ~(" maxoutgoingbytes:" + intparser<unsigned>()) +
            ~(" pinginterval:" + _timedelta()) +
            ~(" pingdeadline:" + _timedelta()) +
            ~(" pinglimit:" + _ratelimiterconfig()) +
            ">")
        .map<rpcconnconfig>(
            [] (const pair<pair<pair< maybe<unsigned>,
                                      maybe<timedelta> >,
                                maybe<timedelta> >,
                           maybe<ratelimiterconfig> > &x) {
                return rpcconnconfig(
                    x.first().first().first().dflt(
                        rpcconnconfig::dflt.maxoutgoingbytes),
                    x.first().first().second().dflt(
                        rpcconnconfig::dflt.pinginterval),
                    x.first().second().dflt(
                        rpcconnconfig::dflt.pingdeadline),
                    x.second().dflt(
                        rpcconnconfig::dflt.pinglimit)); }); }

wireproto_wrapper_type(rpcconn::status_t)
void
rpcconn::status_t::addparam(wireproto::parameter<rpcconnstatus> tmpl,
                            wireproto::tx_message &txm) const {
    wireproto::tx_compoundparameter p;
    p.addparam(proto::rpcconnstatus::outgoing, outgoing);
    p.addparam(proto::rpcconnstatus::fd, fd);
    p.addparam(proto::rpcconnstatus::sequencer, sequencer);
    p.addparam(proto::rpcconnstatus::pendingrx, pendingrx);
    p.addparam(proto::rpcconnstatus::peername, peername_);
    p.addparam(proto::rpcconnstatus::lastcontact, lastcontact);
    if (otherend.isjust()) {
        p.addparam(proto::rpcconnstatus::otherend, otherend.just()); }
    if (otherendtype.isjust()) {
        p.addparam(proto::rpcconnstatus::otherendtype, otherendtype.just()); }
    p.addparam(proto::rpcconnstatus::config, config);
    txm.addparam(
        wireproto::parameter<wireproto::tx_compoundparameter>(tmpl), p); }
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
    doparam(config);
#undef doparam
    rpcconn::status_t res(outgoing.just(),
                          fd.just(),
                          sequencer.just(),
                          peername.just(),
                          lastcontact.just(),
                          p.getparam(proto::rpcconnstatus::otherend),
                          p.getparam(proto::rpcconnstatus::otherendtype),
                          config.just());
    auto r(p.fetch(proto::rpcconnstatus::pendingrx, res.pendingrx));
    if (r.isfailure()) return Nothing;
    else return res; }

const fields::field &
fields::mk(const rpcconn::status_t &o) {
    const field *acc = &("<outgoing: " + mk(o.outgoing) +
                         " fd:" + mk(o.fd) +
                         " sequencer:" + mk(o.sequencer) +
                         " peername:" + mk(o.peername_) +
                         " lastcontact:" + mk(o.lastcontact) +
                         " otherend:" + mk(o.otherend) +
                         " otherendtype:" + mk(o.otherendtype) +
                         " config:" + mk(o.config) +
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

maybe<actortype>
rpcconnauth::type() const {
    if (state == s_done) return ((done *)buf)->type;
    else return Nothing; }

rpcconnauth
rpcconnauth::mkdone(const class slavename &remotename,
                    actortype remotetype,
                    const rpcconnconfig &c,
                    publisher *pub) {
    rpcconnauth r(c, pub);
    r.state = s_done;
    new (r.buf) done(remotename, remotetype);
    if (pub) pub->publish();
    return r; }

rpcconnauth
rpcconnauth::mkwaithello(
    const mastersecret &ms,
    const registrationsecret &rs,
    const rpcconnconfig &c,
    publisher *pub) {
    rpcconnauth r(c, pub);
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
    const registrationsecret &rs,
    const class slavename &_ourname,
    actortype _ourtype,
    const rpcconnconfig &c,
    publisher *pub) {
    rpcconnauth r(c, pub);
    r.state = s_sendhelloslavea;
    new (r.buf) sendhelloslavea(rs, _ourname, _ourtype);
    return r; }

rpcconnauth::sendhelloslavea::sendhelloslavea(
    const registrationsecret &_rs,
    const class slavename &_ourname,
    actortype _ourtype)
    : rs(_rs),
      ourname(_ourname),
      ourtype(_ourtype) {}

rpcconnauth
rpcconnauth::mkwaithelloslavea(
    const registrationsecret &rs,
    waitbox<orerror<void> > *wb,
    const class slavename &_ourname,
    actortype _ourtype,
    const rpcconnconfig &c,
    publisher *pub) {
    rpcconnauth r(c, pub);
    r.state = s_waithelloslavea;
    new (r.buf) waithelloslavea(rs, wb, _ourname, _ourtype);
    return r; }

rpcconnauth::waithelloslavea::waithelloslavea(
    const registrationsecret &_rs,
    waitbox<orerror<void> > *_wb,
    const class slavename &_ourname,
    actortype _ourtype)
    : rs(_rs),
      wb(_wb),
      ourname(_ourname),
      ourtype(_ourtype) {}

rpcconnauth::waithelloslaveb::waithelloslaveb(
    const registrationsecret &_rs,
    const nonce &_n,
    const class slavename &_ourname)
    : rs(_rs),
      n(_n),
      ourname(_ourname) {}

rpcconnauth::rpcconnauth(const rpcconnauth &o)
    : state(o.state),
      pub(o.pub),
      pinglimiter(o.pinglimiter) {
    switch (state) {
        /* Not currently used */
    case s_preinit:
    case s_waithelloslaveb:
    case s_waithelloslavec:
        abort();
    case s_done:
        if (pub) pub->publish();
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
        return; }
    abort(); }

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
    abort(); }

rpcconnauth::rpcconnauth(const rpcconnconfig &c,
                         publisher *_pub)
    : state(s_preinit),
      pub(_pub),
      pinglimiter(c.pinglimit) {}

rpcconnauth::done::done(const class slavename &o,
                        actortype p)
    : slave(o),
      type(p) {}

void
rpcconnauth::start(buffer &b) {
    logmsg(loglevel::info, "start connection in auth state " + fields::mk(state));
    if (state != s_sendhelloslavea) return;
    auto n(nonce::mk());
    auto &ss(*(sendhelloslavea *)buf);
    wireproto::tx_message(proto::HELLOSLAVE::A::tag)
        .addparam(proto::HELLOSLAVE::A::nonce, n)
        .addparam(proto::HELLOSLAVE::A::type, ss.ourtype)
        .serialise(b);
    auto rs(ss.rs);
    auto ourname(ss.ourname);
    ss.~sendhelloslavea();
    new (buf) waithelloslaveb(rs, n, ourname);
    state = s_waithelloslaveb; }

maybe<orerror<wireproto::tx_message *> >
rpcconnauth::message(const wireproto::rx_message &rxm,
                     const peername &peer) {
    typedef maybe<orerror<wireproto::tx_message *> > rtype;
    if (rxm.tag() == proto::PING::tag) {
        /* PING is valid in any authentication state, but rate limited
         * for general sanity. */
        pinglimiter.wait();
        return Nothing; }
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
            return rtype(error::unrecognisedmessage); }
        auto digest(rxm.getparam(proto::HELLO::req::digest));
        auto nonce(rxm.getparam(proto::HELLO::req::nonce));
        auto peername(rxm.getparam(proto::HELLO::req::peername));
        auto _slavename(rxm.getparam(proto::HELLO::req::slavename));
        auto version(rxm.getparam(proto::HELLO::req::version));
        auto flavour(rxm.getparam(proto::HELLO::req::type));
        if (!digest ||
            !nonce ||
            !peername ||
            !_slavename ||
            !version ||
            !flavour) {
            return rtype(error::missingparameter); }
        logmsg(loglevel::verbose,
               "HELLO version " + fields::mk(version) +
               "nonce " + fields::mk(nonce) +
               "peername " + fields::mk(peername) +
               "digest " + fields::mk(digest) +
               "flavour " + fields::mk(flavour));
        if (version.just() != 1) return rtype(error::badversion);
        if (!s->ms.noncevalid(nonce.just(), peername.just())) {
            logmsg(loglevel::notice,
                   "HELLO with invalid nonce from " + fields::mk(peer));
            return rtype(error::authenticationfailed); }
        if (!peername.just().samehost(peer)) {
            logmsg(loglevel::notice,
                   "HELLO with bad host (" + fields::mk(peername.just()) +
                   ", expected host " + fields::mk(peer) +")");
            return rtype(error::authenticationfailed); }
        if (digest.just() != ::digest("B" +
                                      fields::mk(nonce.just()) +
                                      fields::mk(s->rs))) {
            logmsg(loglevel::notice,
                   "HELLO with invalid digest from " + fields::mk(peer));
            return rtype(error::authenticationfailed); }
        logmsg(loglevel::notice, "Valid HELLO from " + fields::mk(peer));
        s->~waithello();
        state = s_done;
        new (buf) done(_slavename.just(), flavour.just());
        if (pub) pub->publish();
        return rtype(new wireproto::resp_message(rxm)); }
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
            return rtype(error::unrecognisedmessage); }
        logmsg(loglevel::info, fields::mk("got a HELLOSLAVE A"));
        auto nonce(rxm.getparam(proto::HELLOSLAVE::A::nonce));
        auto remotetype(rxm.getparam(proto::HELLOSLAVE::A::type));
        if (!nonce || !remotetype) {
            wb->set(error::missingparameter);
            return rtype(error::missingparameter); }
        auto txm(new wireproto::tx_message(proto::HELLOSLAVE::B::tag));
        txm->addparam(proto::HELLOSLAVE::B::digest,
                      ::digest("C" +
                               fields::mk(s->rs) +
                               fields::mk(nonce.just())));
        txm->addparam(proto::HELLOSLAVE::B::name, s->ourname);
        txm->addparam(proto::HELLOSLAVE::B::type, s->ourtype);
        state = s_waithelloslavec;
        s->~waithelloslavea();
        new (buf) waithelloslavec(wb, remotetype.just());
        return rtype(txm); }
    case s_waithelloslaveb: {
        auto s = (waithelloslaveb *)buf;
        if (rxm.tag() != proto::HELLOSLAVE::B::tag) {
            logmsg(loglevel::failure,
                   "received message " +
                   fields::mk(&rxm) +
                   " from " +
                   fields::mk(peer) +
                   "; expected HELLOSLAVE::B");
            return rtype(error::unrecognisedmessage); }
        logmsg(loglevel::info, fields::mk("got a HELLOSLAVE B"));
        auto digest(rxm.getparam(proto::HELLOSLAVE::B::digest));
        auto name(rxm.getparam(proto::HELLOSLAVE::B::name));
        auto remotetype(rxm.getparam(proto::HELLOSLAVE::B::type));
        if (!digest || !name || !remotetype) {
            return rtype(error::missingparameter); }
        if (digest.just() != ::digest("C" +
                                      fields::mk(s->rs) +
                                      fields::mk(s->n))) {
            logmsg(loglevel::notice,
                   "HELLOSLAVE::B with invalid digest from " +
                   fields::mk(peer) + "(" + fields::mk(name.just()) + ")");
            return rtype(error::authenticationfailed); }
        wireproto::tx_message *nextmsg =
            new wireproto::tx_message(proto::HELLOSLAVE::C::tag);
        nextmsg->addparam(proto::HELLOSLAVE::C::name, s->ourname);
        tests::__rpcconn::sendinghelloslavec.trigger(&nextmsg);
        state = s_done;
        s->~waithelloslaveb();
        new (buf) done(name.just(), remotetype.just());
        if (pub) pub->publish();
        return rtype(nextmsg); }
    case s_waithelloslavec: {
        auto s = (waithelloslavec *)buf;
        auto wb(s->wb);
        auto type(s->remotetype);
        auto err(rxm.getparam(wireproto::err_parameter));
        if (rxm.tag() != proto::HELLOSLAVE::C::tag &&
            err == Nothing) {
            logmsg(loglevel::failure,
                   "received message " +
                   fields::mk(&rxm) +
                   " from " +
                   fields::mk(peer) +
                   "; expected HELLOSLAVE::C");
            err = error::unrecognisedmessage; }
        auto name(rxm.getparam(proto::HELLOSLAVE::C::name));
        if (!name && err == Nothing) err = error::missingparameter;
        if (err.isjust()) {
            err.just().warn("HELLOSLAVE C from " + fields::mk(peer)); }
        if (err.isjust()) {
            wb->set(err.just()); }
        else {
            s->~waithelloslavec();
            state = s_done;
            new (buf) done(name.just(), type);
            if (pub) pub->publish();
            wb->set(Success); }
        return rtype(NULL); } }
    abort(); }

/* Only ever called from the connection thread.  Returns true if it's
 * time for the connection to exit and false otherwise. */
bool
rpcconn::queuereply(clientio io, wireproto::tx_message &msg) {
    /* Sending a response has to wait for buffer space, blocking RX
       while it's doing so, because otherwise we don't get the right
       backpressure. .*/
    auto txtoken(txlock.lock());
    if (outgoing.avail() > config.maxoutgoingbytes) {
        tests::__rpcconn::replystopped.trigger();
        subscriber sub;
        subscription ss(sub, shutdown.pub);
        iosubscription ios(sub, sock.poll(POLLOUT));
        while (!shutdown.ready()) {
            /* Note that we hold the lock while we're waiting.  That's
               fine: the only other people who ever acquire it are
               sending messages, and they'll have to wait for buffer
               space the same as us. */
            auto r = sub.wait(io);
            if (r == &ios) {
                auto txres(outgoing.send(io, sock, sub));
                if (txres.isfailure()) {
                    txlock.unlock(&txtoken);
                    txres.failure().warn("clearing space for a reply to " +
                                         fields::mk(peer_));
                    return true; }
                outgoingshrunk.publish();
                if (outgoing.avail() <= config.maxoutgoingbytes) break;
                ios.rearm();
            }
            else {
                assert(r == &ss);
                /* Just re-test shutdown.ready() at top of loop */ } } }
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
    unsigned long history = 0;

    /* Either Nothing if we're waiting to send a ping or the sequence
       of the last ping sent if there's one outstanding. */
    maybe<wireproto::sequencenr> pingsequence(Nothing);
    /* Either the time to send the next ping, if pingsequence == Nothing, or
       the deadline for receiving it, otherwise. */
    timestamp pingtime(timestamp::now() + config.pinginterval);

    {   auto authtok(authlock.lock());
        auth(authtok).start(outgoing);
        authlock.unlock(&authtok); }

    /* Out subscription starts armed to avoid silly races with someone
       queueing something before we start. */
    outarmed = true;

    while (!shutdown.ready()) {
        subscriptionbase *ss = sub.wait(io, pingtime);
        tests::__rpcconn::threadawoken.trigger(this);
      gotss:
        if (ss == NULL) {
            history = (history << 4) | 1;
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
                pingtime = timestamp::now() + config.pingdeadline;
                auto token(txlock.lock());
                wireproto::req_message(proto::PING::tag, pingsequence.just())
                    .serialise(outgoing);
                if (!outarmed) outsub.rearm();
                outarmed = true;
                txlock.unlock(&token); }
        } else if (ss == &shutdownsub) {
            history = (history << 4) | 2;
            continue;
        } else if (ss == &grewsub) {
            history = (history << 4) | 3;
            if (!outarmed) outsub.rearm();
            outarmed = true;
        } else if (ss == &insub) {
            history = (history << 4) | 4;
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
                pingtime = lastcontact_monotone + config.pingdeadline;
            } else {
                pingtime = lastcontact_monotone + config.pinginterval; }
            while (!shutdown.ready()) {
                auto msg(wireproto::rx_message::fetch(inbuffer));
                if (msg.isfailure()) {
                    if (msg.failure() == error::underflowed) {
                        break;
                    } else {
                        msg.failure().warn(
                            "decoding message from " + fields::mk(peer_));
                        goto done; } }
                auto authtok(authlock.lock());
                auto authres(auth(authtok).message(msg.success(), peer_));
                authlock.unlock(&authtok);
                if (authres != Nothing) {
                    if (authres.just().isfailure()) {
                        /* Authentication protocol rejected the
                         * connection.  Tear it down. */
                        goto done; }
                    if (authres.just().success() != NULL) {
                        bool out = queuereply(io, *authres.just().success());
                        delete authres.just().success();
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
                        pingtime = timestamp::now() + config.pinginterval;
                    } else {
                        /* XXX This will leak, with no visible
                         * warning, if we receive a reply we weren't
                         * expecting. */
                        auto token(rxlock.lock());
                        pendingrx.pushtail(msg.success().steal());
                        pendingrxextended.publish();
                        rxlock.unlock(&token);
                        tests::__rpcconn::receivedreply.trigger(); }
                    continue;
                }

                history = (history << 4) | 5;
                auto res(message(msg.success()));

                bool out;
                if (res.issuccess()) {
                    out = queuereply(io, *res.success());
                    delete res.success();
                } else {
                    wireproto::err_resp_message m(msg.success(), res.failure());
                    out = queuereply(io, m); }
                if (out) goto done;
                if (!outarmed) {
                    outsub.rearm();
                    outarmed = true; } }
        } else {
            history = (history << 4) | 6;
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
                    history = (history << 4) | 7;
                    goto gotss; }
            } else {
                /* Don't rearm: the outgoing queue is empty, so we
                 * don't need to. */
                history = (history << 4) | 8;
                txlock.unlock(&token); } } }
    history = (history << 4) | 9;
  done:
    (void)history;
    endconn(io); }

rpcconn::rpcconntoken::rpcconntoken(const thread::constoken &_thr,
                                    socket_t _sock,
                                    const rpcconnauth &__auth,
                                    const rpcconnconfig &_config,
                                    const peername &_peer)
    : thr(_thr),
      sock(_sock),
      auth(__auth),
      config(_config),
      peer(_peer) {}

rpcconn::rpcconn(const rpcconntoken &tok)
    : thread(tok.thr),
      shutdown(),
      sock(tok.sock),
      config(tok.config),
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
      peer_(tok.peer),
      _auth(tok.auth) {}

orerror<wireproto::resp_message *>
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

maybe<actortype>
rpcconn::type() const {
    auto token(authlock.lock());
    auto res(auth(token).type());
    authlock.unlock(&token);
    return res; }

peername
rpcconn::localname() const {
    return sock.localname(); }

rpcconn::sendres
rpcconn::send(
    clientio io,
    const wireproto::tx_message &msg,
    subscriber &sub,
    maybe<timestamp> deadline) {
    auto txtoken(txlock.lock());
    if (outgoing.avail() > config.maxoutgoingbytes) {
        subscription moretx(sub, outgoingshrunk);
        deathsubscription died(sub, this);
        while (outgoing.avail() > config.maxoutgoingbytes) {
            /* Need to drop the TX lock while we're waiting, because
               otherwise the conn thread can't pick it up to actually
               do the send. */
            txlock.unlock(&txtoken);
            if (hasdied() != Nothing) return error::disconnected;
            auto res = sub.wait(io, deadline);
            if (res == NULL) return error::timeout;
            if (res != &moretx && res != &died) return res;
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
    tests::__rpcconn::calldonetx.trigger();
    subscription morerx(sub, pendingrxextended);
    deathsubscription died(sub, this);
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
        if (hasdied() != Nothing) return error::disconnected;
        auto res(sub.wait(io, deadline));
        if (res == NULL) return error::timeout;
        if (res != &morerx && res != &died) return res; } }

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
    sock.close();
    if (!pendingrx.empty()) {
        logmsg(loglevel::failure,
               "shutdown connection to " +
               fields::mk(peer()) +
               " with " +
               fields::mk(pendingrx.length()) +
               " rx pending"); }
    pendingrx.flush(); }

void
rpcconn::drain(clientio io) {
    /* Need to be able to drain the TX buffer even when the main
       connection thread is busy. */
    subscriber sub;
    iosubscription ios(sub, sock.poll(POLLOUT));
    subscription ss(sub, shutdown.pub);
    auto token(txlock.lock());
    auto target(outgoing.offset() + outgoing.avail());
    while (target > outgoing.offset() && !shutdown.ready()) {
        /* Don't want to stop anyone else from queueing more TX while
           we're working -> drop the lock */
        txlock.unlock(&token);
        auto r(sub.wait(io));
        if (r == &ss && shutdown.ready()) return;
        token = txlock.lock();
        if (r == &ios && target > outgoing.offset()) {
            /* Send under the lock, but that's fine because the
               iosubscription fired and we shouldn't ever block. */
            auto txres(outgoing.send(io, sock, sub));
            if (txres.isfailure()) {
                txres.failure().warn("while draining " + fields::mk(peer_));
                break; }
            assert(txres == NULL || txres == &ss);
            ios.rearm(); } }
    txlock.unlock(&token); }

void
rpcconn::destroy(clientio io) {
    drain(io);
    teardown();
    join(io); }

rpcconn::status_t
rpcconn::status() const {
    auto tok1(authlock.lock());
    auto otherend(auth(tok1).slavename());
    auto otherendtype(auth(tok1).type());
    authlock.unlock(&tok1);
    status_t res(outgoing.status(),
                 sock.status(),
                 sequencer.status(),
                 peer_,
                 lastcontact_wall,
                 otherend,
                 otherendtype,
                 config);
    auto tok2(rxlock.lock());
    for (auto it(pendingrx.start()); !it.finished(); it.next()) {
        res.pendingrx.pushtail((*it)->status()); }
    rxlock.unlock(&tok2);
    return res; }

rpcconnstatus::rpcconnstatus(quickcheck q)
    : outgoing(q),
      fd(q),
      sequencer(q),
      pendingrx(),
      peername_(q),
      lastcontact(q),
      otherend(q),
      otherendtype(q),
      config(q) {}

rpcconnstatus::~rpcconnstatus() {
    pendingrx.flush(); }

bool
rpcconnstatus::operator == (const rpcconnstatus &o) const {
    return outgoing == o.outgoing &&
        fd == o.fd &&
        sequencer == o.sequencer &&
        pendingrx.eq(o.pendingrx) &&
        peername_ == o.peername_ &&
        lastcontact == o.lastcontact &&
        otherend == o.otherend &&
        otherendtype == o.otherendtype &&
        config == o.config; }

const rpcconnconfig
rpcconnconfig::dflt(
    /* Max outgoing bytes */
    16384,
    /* Ping interval */
    timedelta::seconds(1),
    /* Ping deadline */
    timedelta::seconds(60),
    /* Ping limiter */
    ratelimiterconfig(
        /* Rate */
        frequency::hz(2),
        /* Bucket size */
        10));
