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
#include "udpsocket.H"
#include "waitbox.H"

orerror<beaconserver *>
beaconserver::build(const registrationsecret &secret,
                    frequency max_response,
                    controlserver *cs,
                    const peername &mastername,
                    const mastersecret &_mastersecret)
{
    auto res = new beaconserver(secret, max_response, cs, mastername,
                                _mastersecret);
    auto r(res->listen());
    if (r.isjust()) {
        delete res;
        return r.just();
    }
    return res;
}

beaconserver::beaconserver(const registrationsecret &_secret,
                           frequency max_response,
                           controlserver *cs,
                           const peername &_mastername,
                           const mastersecret &_mastersecret)
    : statusinterface(this),
      configureinterface(this),
      controlregistration(
          cs->service->registeriface(
              rpcservice<controlconn>::multiregistration()
              .add(statusinterface)
              .add(configureinterface))),
      secret(_secret),
      mastername(_mastername),
      mastersecret_(_mastersecret),
      limiter(max_response, 100),
      listenthreadfn(this),
      listenthread(NULL),
      listenfd(),
      shutdown(),
      errors(0),
      rx(0)
{
}

beaconserver::statusiface::statusiface(beaconserver *server)
    : rpcinterface(proto::BEACONSTATUS::tag),
      owner(server)
{}

messageresult
beaconserver::statusiface::message(const wireproto::rx_message &msg,
                                   controlconn *) {
    auto m(new wireproto::resp_message(msg));
    m->addparam(proto::BEACONSTATUS::resp::secret, owner->secret);
    m->addparam(proto::BEACONSTATUS::resp::limiter,
                owner->limiter.status());
    m->addparam(proto::BEACONSTATUS::resp::errors, owner->errors);
    m->addparam(proto::BEACONSTATUS::resp::rx, owner->rx);
    return m; }

beaconserver::configureiface::configureiface(beaconserver *server)
    : rpcinterface(proto::BEACONCONFIGURE::tag),
      owner(server)
{}

messageresult
beaconserver::configureiface::message(const wireproto::rx_message &,
                                      controlconn *) {
    /* No run-time configuration yet */
    return error::unimplemented; }

maybe<error>
beaconserver::listen()
{
    assert(!listenthread);

    auto r(udpsocket::listen(udpsocket::port(9009)));
    if (r.isfailure()) return r.failure();

    listenfd = r.success();
    auto rr(thread::spawn(&listenthreadfn, &listenthread,
                          fields::mk("beacon listener")));
    if (rr.isjust()) {
        listenfd.close();
        listenfd = udpsocket();
        return rr.just();
    }

    return Nothing;
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
        auto serialiseres(
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
            .serialise(outbuffer));
        if (serialiseres.isjust()) {
            logmsg(loglevel::failure,
                   "error " + fields::mk(serialiseres.just()) +
                   "serialising response to slave " +
                       fields::mk(rr.success()) +
                   " HAIL");
            owner->errors++;
            continue;
        }
        auto sendres(owner->listenfd.send(outbuffer, rr.success()));
        if (sendres.isjust()) {
            logmsg(loglevel::failure,
                   fields::mk(sendres.just()) + " sending HAIL response to " +
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
    if (!listenthread) {
        delete this;
        return; }
    controlregistration->destroy();
    shutdown.set(true);
    listenthread->join(io);
    listenthread = NULL;
    listenfd.close();
    delete this;
}
