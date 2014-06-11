#include "beaconserver.H"

#include <sys/poll.h>
#include <string.h>

#include "buffer.H"
#include "fields.H"
#include "frequency.H"
#include "logging.H"
#include "orerror.H"
#include "peername.H"
#include "proto.H"
#include "udpsocket.H"
#include "waitbox.H"

orerror<beaconserver *>
beaconserver::build(const registrationsecret &secret,
                    frequency max_response,
                    const controlserver &cs)
{
    auto res = new beaconserver(secret, max_response, cs);
    auto r(res->listen());
    if (r.isjust()) {
        delete res;
        return r.just();
    }
    return res;
}

beaconserver::beaconserver(const registrationsecret &_secret,
                           frequency max_response,
                           const controlserver &cs)
    : statusinterface(this),
      configureinterface(this),
      controlregistration(
          cs.registeriface(
              controlserver::multiregistration()
              .add(statusinterface)
              .add(configureinterface))),
      secret(_secret),
      limiter(max_response, 100),
      listenthreadfn(this),
      listenthread(NULL),
      listenfd(),
      shutdown(NULL),
      errors(0),
      rx(0)
{
}

beaconserver::statusiface::statusiface(beaconserver *server)
    : controliface(proto::BEACONSTATUS::tag),
      owner(server)
{}

maybe<error>
beaconserver::statusiface::controlmessage(const wireproto::rx_message &msg,
                                          buffer &outbuf)
{
    wireproto::resp_message m(msg);
    m.addparam(proto::BEACONSTATUS::resp::secret, owner->secret);
    m.addparam(proto::BEACONSTATUS::resp::limiter,
               ratelimiter_status(owner->limiter));
    m.addparam(proto::BEACONSTATUS::resp::errors, owner->errors);
    m.addparam(proto::BEACONSTATUS::resp::rx, owner->rx);
    return m.serialise(outbuf);
}

beaconserver::configureiface::configureiface(beaconserver *server)
    : controliface(proto::BEACONCONFIGURE::tag),
      owner(server)
{}

maybe<error>
beaconserver::configureiface::controlmessage(const wireproto::rx_message &msg,
                                             buffer &outbuf)
{
    wireproto::resp_message m(msg);
    /* No run-time configuration yet */
    return m.serialise(outbuf);
}

maybe<error>
beaconserver::listen()
{
    assert(!listenthread);
    assert(!shutdown);

    auto s(waitbox<bool>::build());
    if (s.isfailure()) return s.failure();
    shutdown = s.success();

    auto r(udpsocket::listen(udpsocket::port(9009)));
    if (r.isfailure()) {
        delete shutdown;
        shutdown = NULL;
        return r.failure();
    }

    listenfd = r.success();
    auto rr(thread::spawn(&listenthreadfn, &listenthread,
                          fields::mk("beacon listener")));
    if (rr.isjust()) {
        listenfd.close();
        listenfd = udpsocket();
        delete shutdown;
        shutdown = NULL;
        return rr.just();
    }

    return Nothing;
}

beaconserver::~beaconserver()
{
    assert(listenthread == NULL);
    assert(shutdown == NULL);
}

beaconserver::listenthreadclass::listenthreadclass(beaconserver *_owner)
    : owner(_owner)
{}

void
beaconserver::listenthreadclass::run()
{
    while (!owner->shutdown->ready()) {
        struct pollfd pfds[2];
        memset(pfds, 0, sizeof(pfds));
        pfds[0] = owner->listenfd.poll();
        pfds[1] = owner->shutdown->fd().poll(POLLIN);
        int r = ::poll(pfds, 2, -1);
        if (r < 0)
            error::from_errno().fatal("polling beacon interface");
        if (!pfds[0].revents)
            continue;
        buffer inbuffer;
        auto rr(owner->listenfd.receive(inbuffer));
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
        auto msg(rrr.success());
        if (msg->t != proto::BEACONPROBE::tag) {
            logmsg(loglevel::failure,
                   "unexpected message tag " +
                   fields::mk(msg->t) +
                   " on beacon interface");
            msg->finish();
            owner->errors++;
            continue;
        }

        logmsg(loglevel::info,
               "received beacon message from " +
               fields::mk(rr.success()));
        owner->rx++;
        msg->finish();
        continue;
    }
}

void
beaconserver::destroy() const
{
    beaconserver *_this = const_cast<beaconserver *>(this);
    if (!shutdown) {
        assert(!listenthread);
        delete this;
        return;
    }
    shutdown->set(true);
    listenthread->join();
    delete shutdown;
    _this->shutdown = NULL;
    _this->listenthread = NULL;
    listenfd.close();
    delete this;
}
