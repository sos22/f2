#include "beaconserver.H"

#include <sys/poll.h>
#include <string.h>

#include "buffer.H"
#include "digest.H"
#include "fields.H"
#include "frequency.H"
#include "logging.H"
#include "nonce.H"
#include "orerror.H"
#include "peername.H"
#include "proto.H"
#include "rpcregistration.H"
#include "rpcservice.H"
#include "test.H"
#include "timedelta.H"
#include "udpsocket.H"
#include "waitbox.H"

#include "test.tmpl"
#include "wireproto.tmpl"

wireproto_wrapper_type(beaconstatus);
void
beaconserver::status_t::addparam(
    wireproto::parameter<beaconserver::status_t> tmpl,
    wireproto::tx_message &tx_msg) const {
    tx_msg.addparam(wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
                    wireproto::tx_compoundparameter()
                    .addparam(proto::beaconstatus::secret, secret)
                    .addparam(proto::beaconstatus::limiter, limiter)
                    .addparam(proto::beaconstatus::errors, errors)
                    .addparam(proto::beaconstatus::rx, rx)); }
maybe<beaconserver::status_t>
beaconserver::status_t::fromcompound(const wireproto::rx_message &msg) {
#define doparam(name)                                           \
    auto name(msg.getparam(proto::beaconstatus::name));         \
    if (!name) return Nothing;
    doparam(secret);
    doparam(limiter);
    doparam(errors);
    doparam(rx);
#undef doparam
    return beaconserver::status_t(secret.just(),
                                  limiter.just(),
                                  errors.just(),
                                  rx.just()); }
const fields::field &
fields::mk(const beaconserver::status_t &o) {
    return
        "<secret:" + mk(o.secret) +
        " limiter:" + mk(o.limiter) +
        " errors:" + mk(o.errors) +
        " rx:" + mk(o.rx) +
        ">"; }

orerror<beaconserver *>
beaconserver::build(const beaconserverconfig &config,
                    controlserver *cs)
{
    auto res = new beaconserver(config, cs);
    auto r(res->listen(config.port_));
    if (r.isfailure()) {
        delete res;
        return r.failure();
    }
    res->statusiface_.start();
    return res;
}

beaconserver::beaconserver(const beaconserverconfig &config,
                           controlserver *cs)
    : statusiface_(this, cs),
      secret(config.rs_),
      mastername(config.coordinator_),
      mastersecret_(config.ms_),
      limiter(config.maxresponses_, 100),
      listenthreadfn(this),
      listenthread(NULL),
      listenfd(),
      shutdown(),
      errors(0),
      rx(0)
{
}

beaconserver::statusiface::statusiface(beaconserver *server,
                                       controlserver *cs)
    : ::statusinterface(cs),
      owner(server) {}

beaconserver::status_t
beaconserver::status() const {
    return status_t(secret, limiter.status(), errors, rx); }

void
beaconserver::statusiface::getstatus(
    wireproto::tx_message *msg) const {
    msg->addparam(proto::STATUS::resp::beacon, owner->status()); }

orerror<void>
beaconserver::listen(peername::port port)
{
    assert(!listenthread);

    auto r(udpsocket::listen(port));
    if (r.isfailure()) return r.failure();

    listenfd = r.success();
    auto rr(thread::spawn(&listenthreadfn, &listenthread,
                          fields::mk("beacon listener")));
    if (!COVERAGE && rr.isfailure()) {
        listenfd.close();
        listenfd = udpsocket();
    }
    return rr;
}

beaconserver::~beaconserver()
{
    assert(listenthread == NULL);
}

beaconserver::listenthreadclass::listenthreadclass(beaconserver *_owner)
    : owner(_owner)
{}

void
beaconserver::listenthreadclass::run(clientio io)
{
    subscriber sub;
    iosubscription iosub(io, sub, owner->listenfd.poll());
    subscription shutdown(sub, owner->shutdown.pub);
    while (!owner->shutdown.ready()) {
        tests::beaconserverreceive.trigger(owner->listenfd);
        auto notified(sub.wait());
        if (notified == &shutdown) continue;
        assert(notified == &iosub);
        buffer inbuffer;
        auto rr(owner->listenfd.receive(inbuffer));
        iosub.rearm();
        if (!owner->limiter.probe()) {
            /* DOS protection: drop things over the rate limit */
            continue;
        }
        if (rr.isfailure()) {
            rr.failure().warn("reading beacon interface");
            owner->errors++;
            /* Shouldn't happen, but back off a little bit if it does,
               just to avoid spamming the logs when things are bad. */
            (timestamp::now() + timedelta::milliseconds(100)).sleep();
            continue;
        }
        auto rrr(wireproto::rx_message::fetch(inbuffer));
        if (rrr.isfailure()) {
            rrr.failure().warn("parsing beacon message");
            owner->errors++;
            continue;
        }
        auto &msg(rrr.success());
        if (msg.tag() != proto::HAIL::tag) {
            logmsg(loglevel::failure,
                   "unexpected message tag " +
                   fields::mk(msg.tag()) +
                   " on beacon interface");
            owner->errors++;
            continue;
        }

        logmsg(loglevel::info,
               "received beacon message from " +
               fields::mk(rr.success()));
        logmsg(loglevel::info, fields::mk("received HAIL"));
        auto reqversion(msg.getparam(proto::HAIL::req::version));
        auto reqnonce(msg.getparam(proto::HAIL::req::nonce));
        if (!reqversion || !reqnonce) {
            logmsg(loglevel::failure,
                   fields::mk("HAIL was missing a mandatory parameter"));
            continue;
        }
        logmsg(loglevel::debug, "version " + fields::mk(reqversion.just()));
        logmsg(loglevel::debug, "nonce " + fields::mk(reqnonce.just()));
        owner->rx++;

        if (reqversion.just() != 1) {
            logmsg(loglevel::failure,
                   "slave " +
                   fields::mk(rr.success()) +
                   " requested bad protocol version " +
                   fields::mk(reqversion.just()) +
                   " in HAIL message");
            owner->errors++;
            continue;
        }

        buffer outbuffer;
        wireproto::tx_message(proto::HAIL::tag)
            .addparam(proto::HAIL::resp::version, 1u)
            .addparam(proto::HAIL::resp::mastername, owner->mastername)
            .addparam(proto::HAIL::resp::slavename, rr.success())
            .addparam(proto::HAIL::resp::nonce, owner->mastersecret_.nonce(
                          rr.success()))
            .addparam(proto::HAIL::resp::slavename, rr.success())
            .addparam(proto::HAIL::resp::digest,
                      digest("A" +
                             fields::mk(owner->mastername) +
                             fields::mk(reqnonce.just()) +
                             fields::mk(owner->secret)))
            .serialise(outbuffer);
        auto sendres(owner->listenfd.send(outbuffer, rr.success()));
        if (!COVERAGE && sendres.isfailure()) {
            sendres.warn("sending HAIL response to " +
                         fields::mk(rr.success()));
            owner->errors++;
        }

        continue;
    }
}

/* Don't want to use an ordinary destructor for this because it can
   wait, and is thus prone to deadlocks if called at the wrong
   time. */
void
beaconserver::destroy(clientio io)
{
    assert(listenthread);
    statusiface_.stop();
    shutdown.set(true);
    listenthread->join(io);
    listenthread = NULL;
    listenfd.close();
    delete this;
}

class tests::event<udpsocket> tests::beaconserverreceive;
