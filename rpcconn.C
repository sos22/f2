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
static const wireproto::parameter<timedelta> pingdeadline(3); }
namespace rpcconnstatus {
static const parameter<class ::bufferstatus> outgoing(1);
static const parameter<class ::fd_tstatus> fd(2);
static const parameter<wireproto::sequencerstatus> sequencer(3);
static const parameter<class ::peername> peername(4);
static const parameter<class ::walltime> lastcontact(5);
static const parameter<class ::rpcconnconfig> config(6);
static const parameter<unsigned> pendingtxcall(7);
static const parameter<unsigned> pendingrxcall(8); } }

rpcconnconfig::rpcconnconfig(unsigned _maxoutgoingbytes,
                             timedelta _pinginterval,
                             timedelta _pingdeadline)
    : maxoutgoingbytes(_maxoutgoingbytes),
      pinginterval(_pinginterval),
      pingdeadline(_pingdeadline) {}
rpcconnconfig::rpcconnconfig(quickcheck q)
    : maxoutgoingbytes(q),
      pinginterval(q),
      pingdeadline(q) {}
wireproto_wrapper_type(rpcconnconfig)
void
rpcconnconfig::addparam(wireproto::parameter<rpcconnconfig> tmpl,
                        wireproto::tx_message &txm) const {
    txm.addparam(wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
                 wireproto::tx_compoundparameter()
                 .addparam(proto::rpcconnconfig::maxoutgoingbytes,
                           maxoutgoingbytes)
                 .addparam(proto::rpcconnconfig::pinginterval, pinginterval)
                 .addparam(proto::rpcconnconfig::pingdeadline, pingdeadline)); }
maybe<rpcconnconfig>
rpcconnconfig::fromcompound(const wireproto::rx_message &p) {
#define doparam(name)                                   \
    auto name(p.getparam(proto::rpcconnconfig::name));  \
    if (!name) return Nothing;
    doparam(maxoutgoingbytes);
    doparam(pinginterval);
    doparam(pingdeadline);
#undef doparam
    return rpcconnconfig(maxoutgoingbytes.just(),
                         pinginterval.just(),
                         pingdeadline.just()); }
bool
rpcconnconfig::operator==(const rpcconnconfig &o) const {
    return maxoutgoingbytes == o.maxoutgoingbytes &&
        pinginterval == o.pinginterval &&
        pingdeadline == o.pingdeadline; }
const fields::field &
fields::mk(const rpcconnconfig &c) {
    return "<rpcconnconfig:"
        " maxoutgoingbytes:" + fields::mk(c.maxoutgoingbytes) +
        " pinginterval:" + fields::mk(c.pinginterval) +
        " pingdeadline:" + fields::mk(c.pingdeadline) +
        ">"; }
const parser<rpcconnconfig> &
parsers::_rpcconnconfig() {
    return ("<rpcconnconfig:" +
            ~(" maxoutgoingbytes:" + intparser<unsigned>()) +
            ~(" pinginterval:" + _timedelta()) +
            ~(" pingdeadline:" + _timedelta()) +
            ">")
        .map<rpcconnconfig>(
            [] (const pair<pair< maybe<unsigned>,
                                 maybe<timedelta> >,
                           maybe<timedelta> > &x) {
                return rpcconnconfig(
                    x.first().first().dflt(
                        rpcconnconfig::dflt.maxoutgoingbytes),
                    x.first().second().dflt(
                        rpcconnconfig::dflt.pinginterval),
                    x.second().dflt(
                        rpcconnconfig::dflt.pingdeadline)); }); }

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
        " config:" + mk(o.config) +
        " pendingtxcall:" + mk(o.pendingtxcall) +
        " pendingrxcall:" + mk(o.pendingrxcall) +
        ">"; }

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
                                    const rpcconnconfig &_config,
                                    const peername &_peer)
    : thr(_thr),
      sock(_sock),
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
      contactlock(),
      lastcontact_monotone(timestamp::now()),
      lastcontact_wall(walltime::now()),
      peer_(tok.peer),
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
    if (msg.tag() == proto::PING::tag) return new wireproto::resp_message(msg);
    else return error::unimplemented; }

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
        config == o.config &&
        pendingtxcall == o.pendingtxcall &&
        pendingrxcall == o.pendingrxcall; }

const rpcconnconfig
rpcconnconfig::dflt(
    /* Max outgoing bytes */
    16384,
    /* Ping interval */
    timedelta::seconds(1),
    /* Ping deadline */
    timedelta::seconds(60));
