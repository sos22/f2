#include "beaconclient.H"

#include "beacon.H"
#include "logging.H"
#include "proto.H"
#include "tuple.H"
#include "version.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "mutex.tmpl"
#include "parsers.tmpl"
#include "rpcservice.tmpl"
#include "thread.tmpl"
#include "wireproto.tmpl"

#include "fieldfinal.H"

class beaconclientslot {
    _beaconclientslotstatus(Z, __mktuplefields, __mktuplefields);
    /* All mutable fields are protected by the main beaconclient
     * lock. */
    /* If we have an un-acknowledged request, the time we sent it.  If
     * we don't, Nothing. */
public: maybe<timestamp> lastbeaconrequest;
    /* When will the entry expire if we fail to refresh it? */
public: timestamp expiry;
    /* When did we receive content of result?  */
public: timestamp lastbeaconresponse;
public:  beaconclientslot(const slavename &,
                          const beaconclientresult &,
                          timestamp,
                          timestamp);
    /* status interface */
public:  typedef beaconclientslotstatus status_t;
public:  status_t status(mutex_t::token) const; };

beaconclientconfig
beaconclientconfig::dflt(const clustername &_cluster,
                         maybe<actortype> _type,
                         const maybe<slavename> &_name) {
    return beaconclientconfig(beaconconfig::dflt,
                              _cluster,
                              _type,
                              _name,
                              timedelta::seconds(1),
                              timedelta::seconds(120)); }
mktupledef(beaconclientconfig)
mktupledef(beaconclientresult)
mktupledef(beaconclientslotstatus)
mktupledef(beaconclientstatus)

const parser<beaconclientconfig> &
parsers::__beaconclientconfig() {
    return ("<beaconclientconfig:" +
            ~(" proto:" + __beaconconfig()) +
            " cluster:" + __clustername() +
            ~(" type:" + _actortype()) +
            ~(" name:" + _slavename()) +
            ~(" queryinterval:" + _timedelta()) +
            ~(" broadcastinterval:" + _timedelta()) +
            ">")
        .map<beaconclientconfig>(
            []
            (const pair<pair<pair<pair<pair<maybe<beaconconfig>,
                                            clustername>,
                                       maybe<actortype> >,
                                  maybe<slavename> >,
                             maybe<timedelta> >,
                        maybe<timedelta> > &x) {
                return beaconclientconfig(
                    x.first().first().first().first().first().dflt(
                        beaconconfig::dflt),
                    x.first().first().first().first().second(),
                    x.first().first().first().second(),
                    x.first().first().second(),
                    x.first().second().dflt(timedelta::seconds(1)),
                    x.second().dflt(timedelta::seconds(120))); }); }

beaconclientslot::beaconclientslot(const slavename &sn,
                                   const beaconclientresult &bcr,
                                   timestamp when,
                                   timestamp _expiry)
    : name(sn),
      result(bcr),
      lastbeaconrequestwall(Nothing),
      lastbeaconrequest(Nothing),
      expiry(_expiry),
      lastbeaconresponse(when) {}

beaconclientslot::status_t
beaconclientslot::status(mutex_t::token /* beacon client lock */) const {
    return status_t(name, result, lastbeaconrequestwall); }

beaconclientstatus::beaconclientstatus(
    const beaconclientconfig &_config,
    int _errors)
    : config(_config),
      cache(),
      errors(_errors) {}

beaconclientstatus::~beaconclientstatus() { }

beaconclient::controliface::controliface(beaconclient *_owner,
                                         controlserver *cs)
    : controlinterface(cs),
      owner(_owner) {
      start(); }

void
beaconclient::controliface::getstatus(rpcservice::response *resp) const {
    resp->addparam(proto::STATUS::resp::beaconclient, owner->status()); }

beaconclient::beaconclient(const thread::constoken &token,
                           const beaconclient::config_t &_config,
                           controlserver *cs,
                           udpsocket _listenfd,
                           udpsocket _clientfd)
    : thread(token),
      config(_config),
      listenfd(_listenfd),
      clientfd(_clientfd),
      errors(0),
      ignored(0),
      _controliface(Nothing) {
      if (cs != NULL) _controliface.mkjust(this, cs); }

orerror<beaconclient *>
beaconclient::build(
    const beaconclientconfig &config,
    controlserver *cs) {
    auto _listenfd(udpsocket::listen(config.proto.respport));
    if (_listenfd.isfailure()) return _listenfd.failure();
    auto _clientfd(udpsocket::client());
    if (_clientfd.isfailure()) {
        _listenfd.success().close();
        return _clientfd.failure(); }
    else return thread::spawn<beaconclient>(
        fields::mk("beaconclient"),
        config,
        cs,
        _listenfd.success(),
        _clientfd.success())
        .go(); }

/* Note that the destructor waits for this without a clientio token ->
 * it must shut down quickly when asked to do so.  The clientio token
 * can only be used to wait for shutdown. */
void
beaconclient::run(clientio io) {
    subscriber sub;
    subscription shutdownsub(sub, shutdown.pub);
    subscription newslavesub(sub, newslave);
    iosubscription listensub(sub, listenfd.poll());
    iosubscription clientsub(sub, clientfd.poll());
    auto nextbroadcast(timestamp::now());
    while (!shutdown.ready()) {
        /* Figure out when we need to wake up for the time-based
         * bit, */
        auto nextactivity(nextbroadcast);
        bool broadcast = false;
        cachelock.locked([&broadcast, &nextactivity, this] (mutex_t::token) {
                for (auto it(cache.start()); !it.finished(); it.next()) {
                    nextactivity = min(nextactivity, it->expiry);
                    if (it->lastbeaconrequest != Nothing) {
                        /* Waiting for a request to come back with a
                         * response -> resend after a short
                         * timeout. */
                        nextactivity = min(
                            nextactivity,
                            it->lastbeaconrequest.just() +
                                config.queryinterval); }
                    else {
                        /* No outstanding requests -> start one once
                         * we're halfway through the existing entry's
                         * expiry time. */
                        nextactivity = min(
                            nextactivity,
                            it->expiry +
                            .5 * (it->expiry - it->lastbeaconresponse)); } } });
        auto notified = sub.wait(io, nextactivity);
        if (notified == &shutdownsub) continue;
        if (notified == &newslavesub) continue;
        if (notified == &listensub || notified == &clientsub) {
            /* Received a response.  Parse it and use it to update the
             * table. */
            buffer inbuffer;
            orerror<peername> rr(error::unknown);
            if (notified == &listensub) {
                rr = listenfd.receive(clientio::CLIENTIO, inbuffer);
                listensub.rearm(); }
            else {
                rr = clientfd.receive(clientio::CLIENTIO, inbuffer);
                clientsub.rearm(); }
            if (rr.isfailure()) {
                rr.failure().warn("reading beacon response");
                errors++;
                /* Stall for a bit to avoid spamming the logs */
                (void)shutdown.get(
                    io, timestamp::now() + timedelta::milliseconds(100));
                continue; }
            auto rrr(wireproto::rx_message::fetch(inbuffer));
            if (rrr.isfailure()) {
                rrr.failure().warn("parsing beacon response from" +
                                   fields::mk(rr.success()));
                errors++;
                continue; }
            auto &msg(rrr.success());
            if (msg.tag() != proto::BEACON::resp::tag) {
                /* Relatively low-severity log messages until we've
                 * checked the version number so that we can in future
                 * change the protocol without changing the port or
                 * completely spamming old clients' logs. */
                logmsg(loglevel::debug,
                       "unexpected message tag " +
                       fields::mk(msg.tag()) +
                       " from " +
                       fields::mk(rr.success()) +
                       " on beacon response port");
                errors++;
                continue; }
            auto respversion(msg.getparam(proto::BEACON::resp::version));
            if (respversion != version::current) {
                logmsg(loglevel::debug,
                       "beacon response version " +
                       fields::mk(respversion) +
                       " from " +
                       fields::mk(rr.success()) +
                       "; expected version " +
                       fields::mk(version::current));
                errors++;
                continue; }
            auto resptype(msg.getparam(proto::BEACON::resp::type));
            auto respname(msg.getparam(proto::BEACON::resp::name));
            auto respcluster(msg.getparam(proto::BEACON::resp::cluster));
            auto respport(msg.getparam(proto::BEACON::resp::port));
            auto respcachetime(msg.getparam(proto::BEACON::resp::cachetime));
            if (!resptype ||
                !respname ||
                !respcluster ||
                !respport ||
                !respcachetime) {
                logmsg(loglevel::failure,
                       "beacon response from " + fields::mk(rr.success()) +
                       " missing mandatory field");
                errors++;
                continue; }
            if (respcluster.just() != config.cluster ||
                (config.type != Nothing && config.type != resptype) ||
                (config.name != Nothing && config.name != respname)) {
                ignored++;
                continue; }
            /* This advertisement is of interest to us.  Refresh the
             * cache. */
            cachelock.locked(
                [this, resptype, &respname, respport, respcachetime, &rr]
                (mutex_t::token) {
                    bool found = false;
                    beaconclientresult bcr(
                        resptype.just(),
                        peername(rr.success().setport(respport.just())),
                        rr.success(),
                        walltime::now());
                    for (auto it(cache.start());
                         !it.finished();
                         it.next()) {
                        if (it->name == respname) {
                            it->result = bcr;
                            found = true;
                            break; } }
                    if (!found) {
                        /* New peer which we've never heard of
                         * before. */
                        cache.append(
                            respname.just(),
                            bcr,
                            timestamp::now(),
                            respcachetime.just() + timestamp::now());
                        _changed.publish(); } });
            continue; }
        /* Must have hit the timeout.  Figure out what we're doing
         * about it. */
        assert(notified == NULL);
        if (timestamp::now() > nextbroadcast) broadcast = true;
        if (broadcast) {
            wireproto::tx_message msg(proto::BEACON::req::tag);
            msg.addparam(proto::BEACON::req::version, version::current);
            msg.addparam(proto::BEACON::req::cluster, config.cluster);
            if (config.name != Nothing) {
                msg.addparam(proto::BEACON::req::name, config.name.just()); }
            if (config.type != Nothing) {
                msg.addparam(proto::BEACON::req::type, config.type.just()); }
            buffer txbuf;
            msg.serialise(txbuf, wireproto::sequencenr::invalid);
            auto r(clientfd.send(txbuf,
                                 peername::udpbroadcast(config.proto.reqport)));
            if (r.isfailure()) {
                r.failure().warn("sending beacon broadcast request");
                errors++; }
            nextbroadcast = timestamp::now() + config.broadcastinterval; }
        cachelock.locked([this, broadcast] (mutex_t::token) {
            bool remove;
            for (auto it(cache.start());
                 !it.finished();
                 remove ? it.remove()
                        : it.next()) {
                if (it->expiry < timestamp::now()) {
                    /* Entry expired */
                    remove = true;
                    continue; }
                remove = false;
                if (broadcast) {
                    /* Sending a broadcast counts as a query on all
                     * peers. */
                    it->lastbeaconrequest = timestamp::now();
                    continue; }
                auto deadline =
                    it->lastbeaconrequest == Nothing
                    ? (/* No outstanding queries -> do it once we're
                          halfway through the cache validity
                          timeout. */
                        it->lastbeaconresponse +
                        .5 * (it->expiry -
                              it->lastbeaconresponse))
                    : (/* Outstanding query -> waiting for peer to
                          respond. */
                        it->lastbeaconrequest.just() + config.queryinterval);
                    if (timestamp::now() > deadline) {
                        /* Time to send another beacon request for
                           this slave. */
                        buffer txbuf;
                        wireproto::tx_message(proto::BEACON::req::tag)
                            .addparam(proto::BEACON::req::version,
                                      version::current)
                            .addparam(proto::BEACON::req::cluster,
                                      config.cluster)
                            .addparam(proto::BEACON::req::name,
                                      it->name)
                            .addparam(proto::BEACON::req::type,
                                      it->result.type)
                            .serialise(txbuf,
                                       wireproto::sequencenr::invalid);
                        auto r(clientfd.send(txbuf, it->result.beacon));
                        if (r.isfailure()) {
                            r.warn("sending beacon directed request");
                            errors++; }
                        /* Ignore errors, apart from logging them;
                           they'll be covered by the usual retry
                           logic anyway. */
                        it->lastbeaconrequest = timestamp::now(); } } } ); }
    /* Control interface is still runnng so this has to be under the
     * cache lock. */
    cachelock.locked([this] (mutex_t::token) { cache.flush(); });
    listenfd.close();
    clientfd.close(); }

maybe<beaconclientresult>
beaconclient::poll(const slavename &sn) {
    return cachelock.locked<maybe<beaconclientresult> >(
        [this, &sn]
        (mutex_t::token) -> maybe<beaconclientresult> {
            for (auto it(cache.start()); !it.finished(); it.next()) {
                if (it->name == sn) return it->result; }
            return Nothing; }); }

beaconclientresult
beaconclient::query(clientio io, const slavename &sn) {
    subscriber sub;
    subscription ss(sub, changed());
    while (true) {
        auto res(poll(sn));
        if (res != Nothing) return res.just();
        sub.wait(io); } }

beaconclient::iterator::entry::entry(const slavename &_name,
                                     actortype _type,
                                     const peername &_peer)
    : name(_name),
      type(_type),
      peer(_peer) {}

beaconclient::iterator::iterator(beaconclient *what,
                                 maybe<actortype> _type)
    : content(),
      it(Nothing) {
    what->cachelock.locked([this, _type, what] (mutex_t::token) {
        for (auto e(what->cache.start()); !e.finished(); e.next()) {
            if (_type == Nothing || _type == e->result.type) {
                content.append(e->name,
                               e->result.type,
                               e->result.server); } } } );
    it.mkjust(const_cast<const list<entry> *>(&content)->start()); }

const slavename &
beaconclient::iterator::name() const { return it.just()->name; }

actortype
beaconclient::iterator::type() const { return it.just()->type; }

const peername &
beaconclient::iterator::peer() const { return it.just()->peer; }

bool
beaconclient::iterator::finished() const { return it.just().finished(); }

void
beaconclient::iterator::next() { it.just().next(); }

beaconclient::iterator::~iterator() { content.flush(); }

beaconclient::iterator
beaconclient::start(maybe<actortype> type) {
    if (type != Nothing && config.type != Nothing && config.type != type) {
        logmsg(loglevel::error,
               "request for peers of type " + fields::mk(type) +
               " on a client which only tracks those of type " +
               fields::mk(config.type)); }
    return iterator(this, type); }

const publisher &
beaconclient::changed() const { return _changed; }

beaconclient::status_t
beaconclient::status() const {
    status_t res(config, errors);
    cachelock.locked([this, &res] (mutex_t::token tok) {
        for (auto it(cache.start()); !it.finished(); it.next()) {
            res.cache.append(it->status(tok)); } });
    return res; }

void
beaconclient::destroy(clientio io) {
    shutdown.set();
    join(io); }

beaconclient::~beaconclient() { }
