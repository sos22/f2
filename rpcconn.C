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
#include "mutex.tmpl"
#include "parsers.tmpl"
#include "rpcconn.tmpl"
#include "test.tmpl"
#include "waitbox.tmpl"
#include "wireproto.tmpl"

tests::event<wireproto::tx_message **> tests::__rpcconn::sendinghelloslavec;
tests::event<rpcconn *> tests::__rpcconn::threadawoken;
tests::event<void> tests::__rpcconn::replystopped;
tests::event<rpcconn *> tests::__rpcconn::calldestroyrace1;
tests::event<rpcconn *> tests::__rpcconn::calldestroyrace2;
tests::event<rpcconn *> tests::__rpcconn::calldestroyrace3;

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
static const parameter<class ::peername> peername(4);
static const parameter<class ::walltime> lastcontact(5);
static const parameter<class ::slavename> otherend(6);
static const parameter<class ::actortype> otherendtype(7);
static const parameter<class ::rpcconnconfig> config(8);
static const parameter<unsigned> pendingtxcall(9);
static const parameter<unsigned> pendingrxcall(10); } }

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
    p.addparam(proto::rpcconnstatus::peername, peername_);
    p.addparam(proto::rpcconnstatus::lastcontact, lastcontact);
    if (otherend.isjust()) {
        p.addparam(proto::rpcconnstatus::otherend, otherend.just()); }
    if (otherendtype.isjust()) {
        p.addparam(proto::rpcconnstatus::otherendtype, otherendtype.just()); }
    p.addparam(proto::rpcconnstatus::config, config);
    p.addparam(proto::rpcconnstatus::pendingtxcall, pendingtxcall);
    p.addparam(proto::rpcconnstatus::pendingrxcall, pendingrxcall);
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
    doparam(pendingtxcall);
    doparam(pendingrxcall);
#undef doparam
    return rpcconn::status_t(outgoing.just(),
                             fd.just(),
                             sequencer.just(),
                             peername.just(),
                             lastcontact.just(),
                             p.getparam(proto::rpcconnstatus::otherend),
                             p.getparam(proto::rpcconnstatus::otherendtype),
                             config.just(),
                             pendingtxcall.just(),
                             pendingrxcall.just()); }

const fields::field &
fields::mk(const rpcconn::status_t &o) {
    return "<outgoing: " + mk(o.outgoing) +
        " fd:" + mk(o.fd) +
        " sequencer:" + mk(o.sequencer) +
        " peername:" + mk(o.peername_) +
        " lastcontact:" + mk(o.lastcontact) +
        " otherend:" + mk(o.otherend) +
        " otherendtype:" + mk(o.otherendtype) +
        " config:" + mk(o.config) +
        " pendingtxcall:" + mk(o.pendingtxcall) +
        " pendingrxcall:" + mk(o.pendingrxcall) +
        ">"; }

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
                    actortype remotetype) {
    rpcconnauth r;
    r.state = s_done;
    new (r.buf) done(remotename, remotetype);
    return r; }

rpcconnauth
rpcconnauth::mkwaithello(
    const mastersecret &ms,
    const registrationsecret &rs,
    const std::function<orerror<void> (orerror<rpcconn *>,
                                       mutex_t::token)> &finished) {
    rpcconnauth r;
    r.state = s_waithello;
    new (r.buf) waithello(ms, rs, finished);
    return r; }

rpcconnauth::waithello::waithello(
    const mastersecret &_ms,
    const registrationsecret &_rs,
    const std::function<orerror<void> (orerror<rpcconn *>,
                                       mutex_t::token)> &_finished)
    : ms(_ms),
      rs(_rs),
      finished(_finished) {}

rpcconnauth::waithello::waithello(const waithello &o)
    : ms(o.ms),
      rs(o.rs),
      finished(o.finished) {}

rpcconnauth
rpcconnauth::mksendhelloslavea(
    const registrationsecret &rs,
    const class slavename &_ourname,
    actortype _ourtype) {
    rpcconnauth r;
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
    actortype _ourtype) {
    rpcconnauth r;
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
    : state(o.state) {
    switch (state) {
        /* Not currently used */
    case s_preinit:
    case s_waithelloslaveb:
    case s_waithelloslavec:
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

rpcconnauth::rpcconnauth()
    : state(s_preinit) {}

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
rpcconnauth::message(rpcconn *conn,
                     const wireproto::rx_message &rxm,
                     const peername &peer,
                     mutex_t::token authtoken) {
    typedef maybe<orerror<wireproto::tx_message *> > rtype;
    if (rxm.tag() == proto::PING::tag) {
        /* PING is valid in any authentication state */
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
        auto finished(s->finished);
        s->~waithello();
        state = s_done;
        new (buf) done(_slavename.just(), flavour.just());
        auto res(finished(conn, authtoken));
        if (res.isfailure()) return rtype(res.failure());
        else return rtype(new wireproto::resp_message(rxm)); }
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
        return rtype(nextmsg); }
    case s_waithelloslavec: {
        auto s = (waithelloslavec *)buf;
        auto wb(s->wb);
        auto _type(s->remotetype);
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
            new (buf) done(name.just(), _type);
            wb->set(Success); }
        return rtype(NULL); } }
    abort(); }

void
rpcconnauth::disconnect(mutex_t::token authlock) {
    switch (state) {
    case s_preinit: abort();
    case s_done: return;
    case s_waithello: {
        waithello *s = (waithello *)buf;
        s->finished(error::disconnected, authlock);
        return; }
    case s_sendhelloslavea: abort();
    case s_waithelloslavea: return;
    case s_waithelloslaveb: return;
    case s_waithelloslavec: return; }
    abort(); }

rpcconnauth &
rpcconn::auth(mutex_t::token) {
    return _auth; }

const rpcconnauth &
rpcconn::auth(mutex_t::token) const {
    return _auth; }

rpcconn::_calls::_send::_send()
    : pending(),
      nrpending(0),
      pub() {}

rpcconn::_calls::_recv::_recv()
    : pending() {}

rpcconn::_calls::_calls()
    : mux(),
      send(),
      recv(),
      finished(false) {}

rpcconn::reftoken
rpcconn::reference() {
    referencelock.locked([this] (mutex_t::token) {
            references++;
            assert(references > 0);
            if (references % 100 == 90) {
                logmsg(loglevel::debug,
                       "runaway reference count? " + fields::mk(references) +
                       " references to " + fields::mk(peer_) +
                       " rpcconn"); }
        });
    /* loadacquire() because it can move earlier without causing a
     * crash but cannot be moved later. */
    assert(loadacquire(referenceable));
    return reftoken(); }

void
rpcconn::unreference(rpcconn::reftoken) {
    referencelock.locked([this] (mutex_t::token token) {
            assert(references > 0);
            references--;
            if (references == 0) referencecond.broadcast(token); }); }

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
                if (outgoing.avail() <= config.maxoutgoingbytes) {
                    sendcalls(txtoken);
                    outgoingshrunk.publish();
                    break; }
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

/* XXX there are potentially some starvation issues here: once we
 * start bumping up against the send queue limit we'll strongly prefer
 * call()s over straight sends, and replies to the peer's calls over
 * either.  We probably don't have enough straight send()s for it to
 * matter, but it's possibly something to worry about in future. */
void
rpcconn::sendcalls(mutex_t::token token) {
    token.formux(txlock);
    calls.mux.locked([this] (mutex_t::token) {
            while (calls.send.nrpending != 0 &&
                   outgoing.avail() <= config.maxoutgoingbytes) {
                auto _call(calls.send.pending.pophead());
                outgoing.transfer(_call->sendbuf);
                calls.send.nrpending--;
                calls.recv.pending.pushtail(_call); }}); }

void
rpcconn::receivereply(wireproto::rx_message *msg) {
    tests::__rpcconn::calldestroyrace2.trigger(this);
    calls.mux.locked(
        [this, msg]
        (mutex_t::token) {
            /* The thing to wake will usually be near the front of the
             * list, assuming that the other end processes requests in
             * order, so a linear scan is just about tolerable. */
            asynccall *completing = NULL;
            for (auto it(calls.recv.pending.start());
                 !it.finished();
                 it.next()) {
                if ((*it)->sequence == msg->sequence()) {
                    completing = *it;
                    it.remove();
                    break; } }
            if (completing == NULL) {
                logmsg(
                    loglevel::error,
                    fields::mk(peer_) +
                    " sent a reply to an unknown message?"
                    " (tag " + fields::mk(msg->tag()) +
                    "; ident " + fields::mk(msg->sequence())+
                    ")");
                /* It's tempting to kill the connection here, but
                 * that's a bad idea because the call must just have
                 * been cancelled after sending, and we don't want
                 * that race to cause us too many problems. */ }
            else {
                auto err(msg->getparam(wireproto::err_parameter));
                completing->mux.locked(
                    [completing, &err, msg, this]
                    (mutex_t::token) {
                        if (err.isjust()) {
                            completing->_result =
                                maybe<orerror<const wireproto::rx_message *> >(
                                    err.just()); }
                        else {
                            completing->_result =
                                maybe<orerror<const wireproto::rx_message *> >(
                                    msg->steal()); } });
                completing->pub.publish(); } });
    tests::__rpcconn::calldestroyrace3.trigger(this); }

void
rpcconn::run(clientio io) {
    subscriber sub;
    subscription shutdownsub(sub, shutdown.pub);
    subscription grewsub(sub, outgoinggrew);
    iosubscription insub(sub, sock.poll(POLLIN));
    iosubscription outsub(sub, sock.poll(POLLOUT));
    subscription callsend(sub, calls.send.pub);
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

    /* Similarly, avoid races with calls which race with our
     * startup. */
    calls.send.pub.publish();

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
        } else if (ss == &callsend) {
            history = (history << 4) | 10;
            if (outgoing.avail() <= config.maxoutgoingbytes) {
                txlock.locked([this] (mutex_t::token t) {sendcalls(t);}); }
            if (!outarmed && !outgoing.empty()) {
                outsub.rearm();
                outarmed = true; }
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
            {   auto contacttoken(contactlock.lock());
                lastcontact_monotone = timestamp::now();
                lastcontact_wall = walltime::now();
                contactlock.unlock(&contacttoken); }
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
                history = (history << 4) | 12;
                auto authtok(authlock.lock());
                auto authres(auth(authtok)
                             .message(this, msg.success(), peer_, authtok));
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
                        receivereply(&msg.success()); }
                    continue;
                }

                /* Bit of a hack: we accept PINGs before
                   authenticating, so have to rate limit them to avoid
                   DOSes, but we don't want to give the message()
                   method a clientio token, so have to do it here. */
                if (msg.success().tag() == proto::PING::tag) {
                    pinglimiter.wait(io); }

                history = (history << 4) | 5;
                auto res(message(msg.success(), messagetoken()));

                bool out;
                switch (res.flavour()) {
                case messageresult::mr_success:
                    out = queuereply(io, *res.success());
                    delete res.success();
                    break;
                case messageresult::mr_failure: {
                    wireproto::err_resp_message m(msg.success(), res.failure());
                    out = queuereply(io, m);
                    break; }
                case messageresult::mr_posted:
                    /* postedcall constructor does all the work */
                    out = false;
                    break;
                }
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
                if (outgoing.avail() <= config.maxoutgoingbytes) {
                    sendcalls(token); }
                if (outgoing.avail() <= config.maxoutgoingbytes) {
                    outgoingshrunk.publish(); }
                if (!outgoing.empty()) {
                    outarmed = true;
                    outsub.rearm(); }
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
                txlock.unlock(&token); }
	    /* Check for completed posted calls */
	    for (auto it(postedcalls.start()); !it.finished(); /**/) {
		bool completed(
		    (*it)->mux.locked<bool>([&it] (mutex_t::token) -> bool {
			    return (*it)->completed; }));
		/* No need for per-call mux here: once the completed
		 * flag is set and whoever set it has dropped the
		 * lock, they will never access it again. */
		if (completed) {
		    (*it)->owner = NULL;
		    delete *it;
		    it.remove(); }
		else it.next(); } } }
    history = (history << 4) | 9;
  done:
    (void)history;

    _threadfinished.set();

    authlock.locked([this] (mutex_t::token token) {
            auth(token).disconnect(token); });

    /* It's too late for any more postedcalls to complete */
    /* (no more can start because we've stopped calling message()) */
    /* We know that whoever owns the locks on individual posted calls
     * will eventually drop them because we've set _threadfinished. */
    while (!postedcalls.empty()) {
	auto c(postedcalls.pophead());
	auto completed(c->mux.locked<bool>([c] (mutex_t::token) {
		    c->owner = NULL;
		    return c->completed; }));
	if (completed) delete c; };

    endconn(io);

    /* endconn is supposed to make sure that nobody creates any more
     * references to the connection. */
    /* storerelease so that it happens after endconn() */
    storerelease(&referenceable, false);

    /* Lost connection -> all outstanding calls fail, no more can be
     * started. */
    calls.mux.locked([this] (mutex_t::token) {
            assert(!calls.finished);
            calls.finished = true;
            if (!calls.send.pending.empty() ||
                !calls.recv.pending.empty()) {
                logmsg(loglevel::info,
                       "lost connection to " + fields::mk(peer_) +
                       " with calls outstanding (" +
                       fields::mk(calls.send.nrpending) + " tx, " +
                       fields::mk(calls.recv.pending.length()) + " rx)"); }
            while (!calls.send.pending.empty()) {
                auto r(calls.send.pending.pophead());
                r->mux.locked([this, r] (mutex_t::token) {
                        if (r->_result.isnothing()) {
                            r->_result =
                                maybe<orerror<const wireproto::rx_message *> >(
                                    error::disconnected);
                            r->pub.publish(); } }); }
            while (!calls.recv.pending.empty()) {
                auto r(calls.recv.pending.pophead());
                r->mux.locked([this, r] (mutex_t::token) {
                        if (r->_result.isnothing()) {
                            r->_result =
                                maybe<orerror<const wireproto::rx_message *> >(
                                    error::disconnected);
                            r->pub.publish(); } }); }});

    /* Wait for any remaining references to drop away. */
    {   auto token(referencelock.lock());
        while (references != 0) token = referencecond.wait(io, &token);
        referencelock.unlock(&token); } }

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
      pinglimiter(config.pinglimit),
      txlock(),
      outgoing(),
      outgoingshrunk(),
      outgoinggrew(),
      sequencelock(),
      sequencer(),
      contactlock(),
      lastcontact_monotone(timestamp::now()),
      lastcontact_wall(walltime::now()),
      peer_(tok.peer),
      _auth(tok.auth),
      referencelock(),
      referencecond(referencelock),
      references(0),
      referenceable(true),
      calls(),
      postedcalls(),
      _threadfinished() {}

enum rpcconn::messageresult::_flavour
rpcconn::messageresult::flavour() const {
    if (content.isfailure()) return mr_failure;
    else if (content.success() == NULL) return mr_posted;
    else return mr_success; }

wireproto::resp_message *
rpcconn::messageresult::success() const {
    assert(flavour() == mr_success);
    return content.success(); }

error
rpcconn::messageresult::failure() const {
    return content.failure(); }

rpcconn::messageresult
rpcconn::message(const wireproto::rx_message &msg, messagetoken) {
    if (msg.tag() == proto::PING::tag) {
        static int cntr;
        return &(*new wireproto::resp_message(msg))
            .addparam(proto::PING::resp::cntr, cntr++);
    } else {
        return error::unimplemented; } }

rpcconn::postedcall::postedcall(
    rpcconn *conn,
    const wireproto::rx_message &rxm,
    rpcconn::messagetoken)
    : resp(rxm),
      owner(conn),
      mux(),
      completed(false) {
    owner->postedcalls.pushtail(this); }

void
rpcconn::postedcall::fail(clientio io, error err) {
    resp.flush();
    resp.addparam(wireproto::err_parameter, err);
    complete(io); }

void
rpcconn::postedcall::complete(clientio io) {
    auto token(mux.lock());
    if (owner == NULL) {
	/* Connection is already shutting down -> drop the reply. */
	completed = true;
	mux.unlock(&token);
	delete this;
	return; }
    /* conn thread should clear owner before it dies, so if we have an
     * owner the thread can't be dead. */
    assert(!owner->hasdied());
    /* Can't use the normal queuereply () or send() methods here
     * because of the way we handle shutdown.  Need to make sure we
     * drop the lock if the connection's thread finishes. */
    auto txtoken(owner->txlock.lock());
    if (owner->outgoing.avail() > owner->config.maxoutgoingbytes) {
	subscriber sub;
	subscription moretx(sub, owner->outgoingshrunk);
	subscription finished(sub, owner->_threadfinished.pub);
	while (owner->outgoing.avail() > owner->config.maxoutgoingbytes) {
	    owner->txlock.unlock(&txtoken);
	    if (owner->_threadfinished.ready()) {
		/* Tell conn thread to delete us when it shuts down.
		 * We can't do it here, because we're still in the
		 * conn's posted call list, and we can't remove
		 * ourselves from the list without either a race or a
		 * deadlock. */
		completed = true;
		mux.unlock(&token);
		return; }
	    mux.unlock(&token);
	    auto r = sub.wait(io);
	    assert(r == &moretx || r == &finished);
	    token = mux.lock();
	    /* Dropped our lock -> owner might now be NULL */
	    if (owner == NULL) {
		completed = true;
		mux.unlock(&token);
		/* Conn thread removes us from the list when it clears
		 * owner -> delete ourselves rather than setting
		 * completed. */
		delete this;
		return; }
	    txtoken = owner->txlock.lock(); } }
    assert(owner->outgoing.avail() <= owner->config.maxoutgoingbytes);
    resp.serialise(owner->outgoing);
    owner->txlock.unlock(&txtoken);
    owner->outgoinggrew.publish();
    /* conn thread will GC us when it wakes up to send the reply we
       just put in the outgoing buffer. */
    completed = true;
    mux.unlock(&token); }

rpcconn::postedcall::~postedcall() {
    assert(completed == true);
    assert(owner == NULL); }

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

orerror<void>
rpcconn::send(
    clientio io,
    const wireproto::tx_message &msg,
    maybe<timestamp> deadline) {
    subscriber sub;
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
            assert(res == &moretx || res == &died);
            txtoken = txlock.lock(); } }
    msg.serialise(outgoing);
    txlock.unlock(&txtoken);
    outgoinggrew.publish();
    return Success; }

rpcconn::asynccall::asynccall(wireproto::sequencenr snr,
                              rpcconn *_owner)
    : sequence(snr.reply()),
      owner(_owner),
      _result(Nothing),
      mux(),
      sendbuf(),
      pub() {}

maybe<orerror<const wireproto::rx_message *> >
rpcconn::asynccall::popresult() {
    auto token(mux.lock());
    auto res(_result);
    /* Return-once rules. */
    if (_result.isjust() &&
        _result.just().issuccess()) {
        assert(_result.just().success() != NULL);
        _result = maybe<orerror<const wireproto::rx_message *> >(NULL); }
    mux.unlock(&token);
    return res; }

void
rpcconn::asynccall::destroy() {
    /* The easy case is that the result is already populated, in which
       case we just need to delete it. */
    int cntr = 0;
    while (!mux.locked<bool>([this] (mutex_t::token) {
                if (_result.isjust()) {
                    if (_result.just().issuccess() &&
                        _result.just().success() != NULL) {
                        delete _result.just().success(); }
                    return true; }
                else {
                    return false; } } )) {
        /* Result is not finished.  Do something more fiddly.  This is a quite
         * racy.  If we hit one of the bad cases we just retry. */
        if (sendbuf.empty()) {
            tests::__rpcconn::calldestroyrace1.trigger(owner);
            /* Already sent the message, so we must be waiting for a
             * response.  Try to pull ourselves out of the list. */
            if (owner->calls.mux.locked<bool>([this] (mutex_t::token) {
                        bool found = false;
                        for (auto it(owner->calls.recv.pending.start());
                             !it.finished();
                             it.next()) {
                            if (*it == this) {
                                it.remove();
                                found = true;
                                break; } }
                        return found; })) {
                /* Removed from list -> we're done. */
                break; }
            else {
                /* Not in list -> must have raced with receive.  Keep
                   going around loop. */ } }
        else {
            /* Waiting to send the message.  We must be in the send
             * pending list. */
            if (owner->calls.mux.locked<bool>([this] (mutex_t::token) {
                        bool found = false;
                        for (auto it(owner->calls.send.pending.start());
                             !it.finished();
                             it.next()) {
                            if (*it == this) {
                                it.remove();
                                assert(owner->calls.send.nrpending > 0);
                                owner->calls.send.nrpending--;
                                found = true;
                                break; } }
                        return found; })) {
                break; } }
        cntr++;
        /* Every iteration of this loop should see the message advance
           through the state machine, which should bound the number we
           need (cntr == 0 -> send.pending, cntr == 1 -> recv.pending
           cntr == 2 -> finished). */
        assert(cntr < 3); }
    /* Killed off the only external references to this -> delete
     * ourselves. */
    delete this; }

rpcconn::asynccall::~asynccall() {}

rpcconn::asynccall *
rpcconn::callasync(const wireproto::req_message &msg) {
    auto res(new asynccall(msg.sequence, this));
    calls.mux.locked([&msg, res, this] (mutex_t::token) {
            if (calls.finished) {
                res->_result =
                    maybe<orerror<const wireproto::rx_message *> >(
                        error::disconnected); }
            else {
                calls.send.nrpending++;
                if (calls.send.nrpending % 100 == 10) {
                    logmsg(loglevel::info,
                           fields::mk(calls.send.nrpending) +
                           " outstanding calls against " +
                           fields::mk(peer()) +
                           "; last queued tag " +
                           fields::mk(msg.t)); }
                msg.serialise(res->sendbuf);
                calls.send.pending.pushtail(res);
                calls.send.pub.publish(); } });
    return res; }

rpcconn::callres
rpcconn::call(
    clientio io,
    const wireproto::req_message &msg,
    subscriber &sub,
    maybe<timestamp> deadline) {
    auto acall((callasync(msg)));
    subscription ss(sub, acall->pub);
    while (1) {
        auto res(acall->popresult());
        if (res.isjust()) {
            ss.detach();
            acall->destroy();
            if (res.just().isfailure()) return res.just().failure();
            else return res.just().success(); }
        auto waitres(sub.wait(io, deadline));
        if (waitres == NULL) {
            ss.detach();
            acall->destroy();
            return error::timeout; }
        if (waitres != &ss) {
            ss.detach();
            acall->destroy();
            return waitres; } } }

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
    assert(!referenceable);
    assert(references == 0);
    sock.close(); }

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
    unsigned nrtx;
    unsigned nrrx;
    calls.mux.locked([&nrrx, &nrtx, this] (mutex_t::token) {
            nrtx = calls.send.nrpending;
            nrrx = calls.recv.pending.length(); });
    status_t res(outgoing.status(),
                 sock.status(),
                 sequencer.status(),
                 peer_,
                 lastcontact_wall,
                 otherend,
                 otherendtype,
                 config,
                 nrtx,
                 nrrx);
    return res; }

rpcconnstatus::rpcconnstatus(quickcheck q)
    : outgoing(q),
      fd(q),
      sequencer(q),
      peername_(q),
      lastcontact(q),
      otherend(q),
      otherendtype(q),
      config(q),
      pendingtxcall(q),
      pendingrxcall(q) {}

bool
rpcconnstatus::operator == (const rpcconnstatus &o) const {
    return outgoing == o.outgoing &&
        fd == o.fd &&
        sequencer == o.sequencer &&
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
