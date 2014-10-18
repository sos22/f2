#include "beaconserver.H"

#include <sys/poll.h>
#include <string.h>

#include "beacon.H"
#include "buffer.H"
#include "digest.H"
#include "fields.H"
#include "frequency.H"
#include "logging.H"
#include "orerror.H"
#include "peername.H"
#include "proto.H"
#include "test.H"
#include "timedelta.H"
#include "udpsocket.H"
#include "version.H"
#include "waitbox.H"

#include "maybe.tmpl"
#include "parsers.tmpl"
#include "rpcservice.tmpl"
#include "test.tmpl"
#include "thread.tmpl"
#include "wireproto.tmpl"

#include "fieldfinal.H"

mktupledef(beaconserverconfig);
mktupledef(beaconserverstatus);

beaconserverconfig
beaconserverconfig::dflt(const clustername &_cluster,
                         const slavename &_slave) {
    return beaconserverconfig(beaconconfig::dflt,
                              _cluster,
                              _slave,
                              timedelta::seconds(60)); }

const parser<beaconserverconfig> &
parsers::__beaconserverconfig() {
    return ("<beaconserverconfig:" +
            ~(" proto:" + parsers::__beaconconfig()) +
            " cluster:" + parsers::__clustername() +
            " name:" + parsers::_slavename() +
            ~(" cachetime:" + parsers::_timedelta()) +
            ">")
        .map<beaconserverconfig>(
            []
            (const pair<pair<pair<maybe<beaconconfig>,
                                  clustername>,
                             slavename>,
                        maybe<timedelta> > &w) {
            return beaconserverconfig(
                w.first().first().first().dflt(beaconconfig::dflt),
                w.first().first().second(),
                w.first().second(),
                w.second().dflt(timedelta::seconds(60))); }); }

beaconserver::controliface::controliface(beaconserver *server,
                                         controlserver *cs)
    : ::controlinterface(cs),
      owner(server) { start(); }

void
beaconserver::controliface::getstatus() const {
    logmsg(loglevel::info, fields::mk(owner->status())); }

orerror<beaconserver *>
beaconserver::build(const beaconserverconfig &config,
                    actortype type,
                    peername::port port,
                    controlserver *cs) {
    auto listensock(udpsocket::listen(config.proto.reqport));
    if (listensock.isfailure()) return listensock.failure();
    auto clientsock(udpsocket::client());
    if (clientsock.isfailure()) {
        listensock.success().close();
        return clientsock.failure(); }
    else return thread::spawn<beaconserver>(
        fields::mk("beaconserver"),
        config,
        type,
        port,
        cs,
        listensock.success(),
        clientsock.success())
             .go(); }

beaconserver::beaconserver(thread::constoken token,
                           const beaconserverconfig &_config,
                           actortype type,
                           peername::port port,
                           controlserver *cs,
                           udpsocket _listenfd,
                           udpsocket _clientfd)
    : thread(token),
      config(_config),
      advertisetype(type),
      advertiseport(port),
      listenfd(_listenfd),
      clientfd(_clientfd),
      shutdown(),
      errors(0),
      ignored(0),
      controliface_(Nothing) { if (cs) controliface_.mkjust(this, cs); }

void
beaconserver::run(clientio io) {
    subscriber sub;
    iosubscription listensub(sub, listenfd.poll());
    iosubscription clientsub(sub, clientfd.poll());
    subscription shutdownsub(sub, shutdown.pub);

    /* Response is always the same, so pre-populate it. */
    wireproto::tx_message response(proto::BEACON::resp::tag);
    response
        .addparam(proto::BEACON::resp::version, version::current)
        .addparam(proto::BEACON::resp::type, advertisetype)
        .addparam(proto::BEACON::resp::name, config.name)
        .addparam(proto::BEACON::resp::cluster, config.cluster)
        .addparam(proto::BEACON::resp::port, advertiseport)
        .addparam(proto::BEACON::resp::cachetime, config.cachetime);

    /* Broadcast our existence as soon as we start, to make things a
     * bit easier on the clients.  If this gets lost then the periodic
     * client-driven request broadcasts will eventually recover. */
    {   buffer buf;
        response.serialise(buf, wireproto::sequencenr::invalid);
        auto r(clientfd.send(buf,
                             peername::udpbroadcast(config.proto.respport)));
        if (r.isfailure()) {
            r.failure().warn("sending beacon broadcast");
            errors++; } }

    while (!shutdown.ready()) {
        auto notified(sub.wait(io));
        if (notified == &shutdownsub) continue;
        buffer inbuffer;
        orerror<peername> rr(error::unknown);
        /* Process the request we just received.  receive() won't
         * block because the iosub is notified, so doesn't need a
         * clientio token. */
        if (notified == &listensub) {
            rr = listenfd.receive(clientio::CLIENTIO, inbuffer);
            listensub.rearm(); }
        else {
            assert(notified == &clientsub);
            rr = clientfd.receive(clientio::CLIENTIO, inbuffer);
            clientsub.rearm(); }
        if (rr.isfailure()) {
            rr.failure().warn("reading beacon interface");
            errors++;
            /* Shouldn't happen, but back off a little bit if it does,
               just to avoid spamming the logs when things are bad. */
            (void)shutdown.get(io,
                               timestamp::now() + timedelta::milliseconds(100));
            continue; }
        auto rrr(wireproto::rx_message::fetch(inbuffer));
        /* Low logging level until we've confirmed it's the right
         * version, to avoid spamming the logs on old systems when we
         * introduce version 2. */
        if (rrr.isfailure()) {
            logmsg(loglevel::debug,
                   "parsing beacon message: " + fields::mk(rrr.failure()));
            errors++;
            continue; }
        auto &msg(rrr.success());
        if (msg.tag() != proto::BEACON::req::tag) {
            logmsg(loglevel::debug,
                   "unexpected message tag " + fields::mk(msg.tag()) +
                   " on beacon interface");
            errors++;
            continue; }

        auto reqversion(msg.getparam(proto::BEACON::req::version));
        if (reqversion != version::current) {
            logmsg(loglevel::debug,
                   "BEACON request from " + fields::mk(rr.success())
                   + " asked for bad version "
                   + fields::mk(reqversion));
            errors++;
            continue; }
        auto reqcluster(msg.getparam(proto::BEACON::req::cluster));
        if (!reqcluster) {
            logmsg(loglevel::failure,
                   "BEACON request from " + fields::mk(rr.success()) +
                   "missing mandatory parameter");
            errors++;
            continue; }
        logmsg(loglevel::info,
               "received beacon message from " +
               fields::mk(rr.success()));
        auto reqname(msg.getparam(proto::BEACON::req::name));
        auto reqtype(msg.getparam(proto::BEACON::req::type));

        if (reqcluster != config.cluster ||
            (reqname != Nothing && reqname != config.name) ||
            (reqtype != Nothing && reqtype != advertisetype)) {
            logmsg(loglevel::debug,
                   "BEACON request from " +
                   fields::mk(rr.success()) +
                   " asked for cluster " + fields::mk(reqcluster) +
                   " name " + fields::mk(reqname) +
                   " type " + fields::mk(reqtype) +
                   "; we are cluster " + fields::mk(config.cluster) +
                   " name " + fields::mk(config.name) +
                   " type " + fields::mk(advertisetype) +
                   "; ignoring");
            ignored++;
            continue; }

        buffer outbuffer;
        response.serialise(outbuffer, wireproto::sequencenr::invalid);
        /* Note that we always send on the client fd, even for
         * requests for come in on the listen fd.  That's because the
         * client will use whichever port we send from when it wants
         * to refresh its cache and every beaconserver uses the same
         * listenfd port and a unique clientfd one, so things are just
         * generally easier if we can arrange for clients who want to
         * talk to us specifically use the client socket rather than
         * the listen one. */
        auto sendres(clientfd.send(outbuffer, rr.success()));
        if (sendres.isfailure()) {
            sendres.warn("sending HAIL response to " +
                         fields::mk(rr.success()));
            errors++; } }
    listenfd.close(); }

void
beaconserver::destroy(clientio io) {
    shutdown.set();
    join(io); }

beaconserver::~beaconserver() {}

beaconserver::status_t
beaconserver::status() const {
    return status_t(config, errors, ignored, advertisetype, advertiseport); }
