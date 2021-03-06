#include "beaconserver.H"

#include <sys/poll.h>
#include <string.h>

#include "beacon.H"
#include "buffer.H"
#include "digest.H"
#include "fields.H"
#include "logging.H"
#include "orerror.H"
#include "peername.H"
#include "test.H"
#include "timedelta.H"
#include "udpsocket.H"
#include "version.H"
#include "waitbox.H"

#include "fields.tmpl"
#include "maybe.tmpl"
#include "parsers.tmpl"
#include "test.tmpl"
#include "thread.tmpl"
#include "waitbox.tmpl"

beaconserverconfig::beaconserverconfig(const beaconconfig &_proto,
                                       const clustername &_cluster,
                                       const agentname &_name,
                                       timedelta _cachetime)
    : proto(_proto),
      cluster(_cluster),
      name(_name),
      cachetime(_cachetime) {}

beaconserverconfig::beaconserverconfig(deserialise1 &ds)
    : proto(ds),
      cluster(ds),
      name(ds),
      cachetime(ds) {}

void
beaconserverconfig::serialise(serialise1 &s) const {
    s.push(proto);
    s.push(cluster);
    s.push(name);
    s.push(cachetime); }

bool
beaconserverconfig::operator==(const beaconserverconfig &o) const {
    return proto == o.proto &&
        cluster == o.cluster &&
        name == o.name &&
        cachetime == o.cachetime; }

const parser<beaconserverconfig> &
beaconserverconfig::parser() {
    auto &i("<beaconserverconfig:" +
            ~(" proto:" + beaconconfig::parser()) +
            " cluster:" + clustername::parser() +
            " name:" + agentname::parser() +
            ~(" cachetime:" + timedelta::parser()) +
            ">");
    class f : public ::parser<beaconserverconfig> {
    public: decltype(i) inner;
    public: f(decltype(inner) _inner) : inner(_inner) {}
    public: orerror<result> parse(const char *what) const {
        auto i(inner.parse(what));
        if (i.isfailure()) return i.failure();
        auto &w(i.success().res);
        return result(i.success().left,
                      beaconserverconfig(
                          w.first().first().first().dflt(beaconconfig::dflt),
                          w.first().first().second(),
                          w.first().second(),
                          w.second().dflt(timedelta::seconds(60)))); } };
    return *new f(i); }

const fields::field &
beaconserverconfig::field() const {
    return
        "<beaconserverconfig:"
        " proto:" + proto.field() +
        " cluster:" + cluster.field() +
        " name:" + name.field() +
        " cachetime:" + cachetime.field() +
        ">"; }

beaconserverconfig
beaconserverconfig::dflt(const clustername &_cluster,
                         const agentname &_agent) {
    return beaconserverconfig(beaconconfig::dflt,
                              _cluster,
                              _agent,
                              timedelta::seconds(60)); }

orerror<beaconserver *>
beaconserver::build(const beaconserverconfig &config,
                    const list<interfacetype> &type,
                    peername::port port) {
    auto listensock(udpsocket::listen(config.proto.reqport()));
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
        listensock.success(),
        clientsock.success())
             .go(); }

beaconserver::beaconserver(thread::constoken token,
                           const beaconserverconfig &_config,
                           const list<interfacetype> &type,
                           peername::port port,
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
      ignored(0) {}

void
beaconserver::run(clientio io) {
    subscriber sub;
    iosubscription listensub(sub, listenfd.poll());
    iosubscription clientsub(sub, clientfd.poll());
    subscription shutdownsub(sub, shutdown.pub());

    /* Response is always the same, so pre-populate it. */
    proto::beacon::resp response(config.cluster,
                                 config.name,
                                 advertisetype,
                                 advertiseport,
                                 config.cachetime);

    /* Broadcast our existence as soon as we start, to make things a
     * bit easier on the clients.  If this gets lost then the periodic
     * client-driven request broadcasts will eventually recover. */
    {   buffer buf;
        serialise1 s(buf);
        response.serialise(s);
        auto r(clientfd.send(buf,
                             peername::udpbroadcast(config.proto.respport())));
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
        deserialise1 ds(inbuffer);
        proto::beacon::req req(ds);
        /* Low logging level until we've confirmed it's the right
         * version, to avoid spamming the logs on old systems when we
         * introduce version 2. */
        if (ds.isfailure() || req.version != version::current) {
            logmsg(loglevel::debug,
                   "parsing beacon message: " + fields::mk(ds.failure()));
            errors++;
            continue; }

        if (req.cluster != config.cluster ||
            (req.name != Nothing && req.name != config.name) ||
            (req.type != Nothing && !advertisetype.contains(req.type.just()))) {
            logmsg(loglevel::debug,
                   "BEACON request from " +
                   fields::mk(rr.success()) +
                   " asked for cluster " + fields::mk(req.cluster) +
                   " name " + fields::mk(req.name) +
                   " type " + fields::mk(req.type) +
                   "; we are cluster " + fields::mk(config.cluster) +
                   " name " + fields::mk(config.name) +
                   " type " + fields::mk(advertisetype) +
                   "; ignoring");
            ignored++;
            continue; }

        logmsg(loglevel::info,
               "received beacon request from " +
               fields::mk(rr.success()) + " on " +
               fields::mk(notified == &listensub
                          ? "listensub"
                          : "clientsub"));

        buffer outbuffer;
        serialise1 s(outbuffer);
        response.serialise(s);
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

void
beaconserver::status() {
    logmsg(loglevel::info, "beacon server");
    logmsg(loglevel::info, "config: " + config.field());
    logmsg(loglevel::info, "advertise: " + advertisetype.field());
    logmsg(loglevel::info, "port: " + advertiseport.field());
    logmsg(loglevel::info, "listenfd: " + listenfd.asfd().field());
    logmsg(loglevel::info, "clientfd: " + listenfd.asfd().field());
    logmsg(loglevel::info, "shutdown: " + shutdown.field());
    logmsg(loglevel::info, "errors: " + fields::mk(errors));
    logmsg(loglevel::info, "ignored: " + fields::mk(ignored)); }
