#include "beaconclient.H"

#include "beacon.H"
#include "buffer.H"
#include "logging.H"
#include "quickcheck.H"
#include "version.H"

#include "fields.tmpl"
#include "list.tmpl"
#include "maybe.tmpl"
#include "mutex.tmpl"
#include "pair.tmpl"
#include "parsers.tmpl"
#include "thread.tmpl"

beaconclientconfig::beaconclientconfig(
    const clustername &__cluster,
    maybe<interfacetype> __type,
    const maybe<agentname> &__agent,
    const beaconconfig &__beacon)
    : beaconclientconfig(__cluster,
                         __type,
                         __agent,
                         __beacon,
                         timedelta::seconds(1),
                         timedelta::minutes(2)) {}

orerror<beaconclientconfig>
beaconclientconfig::mk(const clustername &cluster,
                       maybe<interfacetype> type,
                       const maybe<agentname> &agent,
                       const beaconconfig &beacon,
                       timedelta queryinterval,
                       timedelta broadcastinterval) {
    if (queryinterval < timedelta::seconds(0) ||
        broadcastinterval < timedelta::seconds(0)) {
        return error::range; }
    else return beaconclientconfig(cluster,
                                   type,
                                   agent,
                                   beacon,
                                   queryinterval,
                                   broadcastinterval); }

beaconclientconfig::beaconclientconfig(const clustername &__cluster,
                                       maybe<interfacetype> __type,
                                       const maybe<agentname> &__agent,
                                       const beaconconfig &__beacon,
                                       timedelta __queryinterval,
                                       timedelta __broadcastinterval)
    : _cluster(__cluster),
      _type(__type),
      _name(__agent),
      _proto(__beacon),
      _queryinterval(__queryinterval),
      _broadcastinterval(__broadcastinterval) {
    assert(__queryinterval >= timedelta::seconds(0));
    assert(__broadcastinterval >= timedelta::seconds(0)); }

const parser<beaconclientconfig> &
parsers::__beaconclientconfig() {
    return ("<beaconclientconfig: " + __clustername() +
            ~(" type:" + _maybe(interfacetype::parser())) +
            ~(" name:" + _maybe(_agentname())) +
            ~(" proto:" + beaconconfig::parser()) +
            ~(" queryinterval:" + _timedelta()) +
            ~(" broadcastinterval:" + _timedelta()) +
            ">")
        .maperr<beaconclientconfig>(
            []
            (const pair<pair<pair<pair<pair<clustername,
                                            maybe<maybe<interfacetype> > >,
                                       maybe<maybe<agentname> > >,
                                  maybe<beaconconfig> >,
                             maybe<timedelta> >,
                        maybe<timedelta> > &x) {
                return  beaconclientconfig::mk(
                        x.first().first().first().first().first(),
                        x.first().first().first().first().second()
                            .dflt(Nothing),
                        x.first().first().first().second()
                            .dflt(Nothing),
                        x.first().first().second()
                            .dflt(beaconconfig::dflt),
                        x.first().second()
                            .dflt(timedelta::seconds(1)),
                        x.second()
                            .dflt(timedelta::minutes(1))); }); }

void
beaconclientconfig::serialise(serialise1 &s) const {
    s.push(_cluster);
    s.push(_type);
    s.push(_name);
    s.push(_proto);
    s.push(_queryinterval);
    s.push(_broadcastinterval); }

beaconclientconfig::beaconclientconfig(deserialise1 &ds)
    : _cluster(ds),
      _type(ds),
      _name(ds),
      _proto(ds),
      _queryinterval(ds),
      _broadcastinterval(ds) {
    if (_queryinterval < 0_s) {
        ds.fail(error::range);
        _queryinterval = 0_s; }
    if (_broadcastinterval < 0_s) {
        ds.fail(error::range);
        _broadcastinterval = 0_s; } }

bool
beaconclientconfig::operator==(const beaconclientconfig &o) const {
    return !(*this != o); }

bool
beaconclientconfig::operator!=(const beaconclientconfig &o) const {
    return _cluster != o._cluster ||
        _type != o._type ||
        _name != o._name ||
        _proto != o._proto ||
        _queryinterval != o._queryinterval ||
        _broadcastinterval != o._broadcastinterval; }

const fields::field &
beaconclientconfig::field() const {
    return "<beaconclientconfig: " + fields::mk(_cluster) +
        " type:" + fields::mk(_type) +
        " name:" + fields::mk(_name) +
        " proto:" + fields::mk(_proto) +
        " queryinterval:" + fields::mk(_queryinterval) +
        " broadcastinterval:" + fields::mk(_broadcastinterval) +
        ">"; }

const fields::field &
fields::mk(const beaconclientconfig &o) { return o.field(); }

class beaconclientslot {
    friend class beaconclient;
    /* Name of remote system */
private: agentname name;
    /* Interface exposed by remote system */
private: list<interfacetype> type;
    /* Where to connect to for this server? */
private: peername server;
    /* Where is the beacon for this server? */
private: peername beacon;
    /* When did we last get a beacon response for this agent? */
private: walltime lastbeaconresponsewall;

    /* All mutable fields are protected by the main beaconclient
     * lock. */
    /* If we have an un-acknowledged request, the time we sent it.  If
     * we don't, Nothing. */
public: maybe<timestamp> lastbeaconrequest;
    /* When will the entry expire if we fail to refresh it? */
public: timestamp expiry;
    /* When did we receive content of result?  */
public: timestamp lastbeaconresponse;
public:  beaconclientslot(const agentname &,
                          const list<interfacetype> &,
                          const peername &,
                          const peername &,
                          walltime,
                          timestamp,
                          timestamp);
public:  void status(mutex_t::token, loglevel) const;
public:  timestamp nextsend(mutex_t::token,
                            const beaconclientconfig &) const; };

beaconclientslot::beaconclientslot(const agentname &sn,
                                   const list<interfacetype> &_type,
                                   const peername &_server,
                                   const peername &_beacon,
                                   walltime now,
                                   timestamp when,
                                   timestamp _expiry)
    : name(sn),
      type(_type),
      server(_server),
      beacon(_beacon),
      lastbeaconresponsewall(now),
      lastbeaconrequest(Nothing),
      expiry(_expiry),
      lastbeaconresponse(when) {}

void
beaconclientslot::status(mutex_t::token /* beacon client lock */,
                         loglevel level) const {
    logmsg(level,
           "name: " + fields::mk(name) +
           " type: " + fields::mk(type) +
           " server: " + fields::mk(server) +
           " beacon: " + fields::mk(beacon) +
           " lastbeaconresponsewall: " + fields::mk(lastbeaconresponsewall) +
           " lastbeaconrequest: " + fields::mk(lastbeaconrequest) +
           " expiry: " + fields::mk(expiry) +
           " lastbeaconresponse: " + fields::mk(lastbeaconresponse)); }

beaconclient::beaconclient(const thread::constoken &token,
                           const beaconclient::config_t &_config,
                           udpsocket _listenfd,
                           udpsocket _clientfd)
    : thread(token),
      config(_config),
      listenfd(_listenfd),
      clientfd(_clientfd),
      errors(0),
      ignored(0) {}

orerror<nnp<beaconclient> >
beaconclient::build(const beaconclientconfig &config) {
    auto _listenfd(udpsocket::listen(config.proto().respport()));
    if (_listenfd.isfailure()) return _listenfd.failure();
    auto _clientfd(udpsocket::client());
    if (_clientfd.isfailure()) {
        _listenfd.success().close();
        return _clientfd.failure(); }
    else return _nnp(*thread::start<beaconclient>(
                         fields::mk("beaconclient"),
                         config,
                         _listenfd.success(),
                         _clientfd.success())); }

timestamp
beaconclientslot::nextsend(mutex_t::token /* client lock */,
                           const beaconclientconfig &config) const {
    if (lastbeaconrequest != Nothing) {
        /* Sent a request, waiting for a response -> resend after a
         * short timeout. */
        return lastbeaconrequest.just() + config.queryinterval(); }
    else {
        /* No outstanding requests -> start one once we're halfway
         * through the existing entry's expiry time. */
        return expiry - (expiry - lastbeaconresponse) / 2; } }

void
beaconclient::sendbroadcast() {
    logmsg(loglevel::verbose, "send broadcast");
    proto::beacon::req req(config.cluster(), config.name(), config.type());
    buffer txbuf;
    serialise1 s(txbuf);
    req.serialise(s);
    auto r(clientfd.send(
               txbuf, peername::udpbroadcast(config.proto().reqport())));
    if (r.isfailure()) {
        r.failure().warn("sending beacon broadcast request");
        errors++;
        return; }
    /* A broadcast counts as a query on all known agents. */
    cachelock.locked([this] (mutex_t::token) {
            for (auto it(cache.start()); !it.finished(); it.next()) {
                it->lastbeaconrequest = timestamp::now(); } }); }
void
beaconclient::handletimeouts(mutex_t::token tok) {
    tok.formux(cachelock);
    bool remove;
    for (auto it(cache.start());
         !it.finished();
         remove ? it.remove() : it.next()) {
        if (it->expiry < timestamp::now()) {
            /* Entry expired */
            remove = true;
            logmsg(loglevel::verbose, "expired:");
            it->status(tok, loglevel::verbose);
            continue; }
        remove = false;
        if (timestamp::now() < it->nextsend(tok, config)) continue;
        /* Time to send another beacon request for this agent. */
        buffer txbuf;
        serialise1 s(txbuf);
        proto::beacon::req(config.cluster(), it->name, Nothing).serialise(s);
        logmsg(loglevel::verbose, "query on:");
        it->status(tok, loglevel::verbose);
        auto r(clientfd.send(txbuf, it->beacon));
        if (r.isfailure()) {
            r.warn("sending beacon directed request");
            errors++; }
        /* Ignore errors, apart from logging them; they'll
           be covered by the usual retry logic anyway. */
        it->lastbeaconrequest = timestamp::now(); } }

/* Must shut down quickly once shutdown is set. */
void
beaconclient::run(clientio io) {
    subscriber sub;
    subscription shutdownsub(sub, shutdown.pub());
    iosubscription listensub(sub, listenfd.poll());
    iosubscription clientsub(sub, clientfd.poll());
    auto nextbroadcast(timestamp::now());
    while (!shutdown.ready()) {
        /* Figure out when we need to wake up for the time-based
         * bit, */
        auto nextactivity(nextbroadcast);
        cachelock.locked(
            [&nextactivity, this]
            (mutex_t::token token) {
                for (auto it(cache.start()); !it.finished(); it.next()) {
                    nextactivity = min(nextactivity, it->expiry);
                    nextactivity = min(nextactivity,
                                       it->nextsend(token, config)); } });
        logmsg(loglevel::verbose, "sleep to " + fields::mk(nextactivity));
        auto notified = sub.wait(io, nextactivity);
        if (notified == &shutdownsub) continue;

        /* Always run timeout processing, even if we were notified for
         * something else first, because it seems to make the
         * behaviour a little bit more predictable when beaconclient
         * startup races with a new beacon server arriving.  That
         * mostly only happens in the test suite. */
        if (timestamp::now() >= nextbroadcast) {
            sendbroadcast();
            nextbroadcast = timestamp::now() + config.broadcastinterval(); }
        cachelock.locked([this] (mutex_t::token tok) { handletimeouts(tok); } );
        if (notified == NULL) continue;
        assert(notified == &listensub || notified == &clientsub);

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
        deserialise1 ds(inbuffer);
        proto::beacon::resp resp(ds);
        /* Use relatively low-severity log messages until we've
         * checked the version number so that we can in future change
         * the protocol without changing the port or completely
         * spamming old clients' logs. */
        if (ds.isfailure()) {
            logmsg(loglevel::debug,
                   "parsing beacon response from" +
                   fields::mk(rr.success()) +
                   ": " + fields::mk(ds.failure()));
            errors++;
            continue; }
        if (resp.version != version::current) {
            logmsg(loglevel::debug,
                   "beacon response version " +
                   fields::mk(resp.version) +
                   " from " +
                   fields::mk(rr.success()) +
                   "; expected version " +
                   fields::mk(version::current));
            errors++;
            continue; }
        if (resp.cluster != config.cluster() ||
            (config.type() != Nothing &&
             !resp.type.contains(config.type().just())) ||
            (config.name() != Nothing && config.name() != resp.name)) {
            ignored++;
            continue; }
        /* This advertisement is of interest to us.  Refresh the
         * cache. */
        cachelock.locked(
            [this, &resp, &rr]
            (mutex_t::token tok) {
                bool found = false;
                peername server(rr.success().setport(resp.port));
                for (auto it(cache.start());
                     !it.finished();
                     it.next()) {
                    if (it->name == resp.name) {
                        it->type = resp.type;
                        it->server = server;
                        it->beacon = rr.success();
                        it->lastbeaconresponsewall = walltime::now();
                        it->lastbeaconresponse = timestamp::now();
                        it->lastbeaconrequest = Nothing;
                        it->expiry = resp.cachetime + timestamp::now();
                        logmsg(loglevel::verbose, "refreshed:");
                        it->status(tok, loglevel::verbose);
                        found = true;
                        break; } }
                if (!found) {
                    /* New peer which we've never heard of
                     * before. */
                    auto &i(cache.append(
                                resp.name,
                                resp.type,
                                server,
                                rr.success(),
                                walltime::now(),
                                timestamp::now(),
                                resp.cachetime + timestamp::now()));
                    logmsg(loglevel::verbose, "new:");
                    i.status(tok, loglevel::verbose);
                    _changed.publish(); } }); }
    cache.flush();
    listenfd.close();
    clientfd.close(); }

beaconclient::result::result(const peername &_name,
                             const list<interfacetype> &_type)
    : name(_name),
      type(_type) {}

maybe<beaconclient::result>
beaconclient::poll(const agentname &sn) const {
    return cachelock.locked<maybe<result> >(
        [this, &sn]
        (mutex_t::token) -> maybe<result> {
            for (auto it(cache.start()); !it.finished(); it.next()) {
                if (it->name == sn) return result(it->server, it->type); }
            return Nothing; }); }

beaconclient::result
beaconclient::query(clientio io, const agentname &sn) const {
    subscriber sub;
    subscription ss(sub, changed());
    while (true) {
        auto res(poll(sn));
        if (res != Nothing) return res.just();
        sub.wait(io); } }

beaconclient::iterator::entry::entry(const agentname &_name,
                                     const list<interfacetype> &_type,
                                     const peername &_peer)
    : name(_name),
      type(_type),
      peer(_peer) {}

beaconclient::iterator::iterator(const beaconclient &what,
                                 maybe<interfacetype> _type)
    : content(),
      it(Nothing) {
    what.cachelock.locked([this, _type, &what] (mutex_t::token) {
            for (auto e(what.cache.start()); !e.finished(); e.next()) {
                if (_type == Nothing || e->type.contains(_type.just())) {
                    content.append(e->name, e->type, e->server); } } } );
    it.mkjust(const_cast<const list<entry> *>(&content)->start()); }

const agentname &
beaconclient::iterator::name() const { return it.just()->name; }

const list<interfacetype> &
beaconclient::iterator::type() const { return it.just()->type; }

const peername &
beaconclient::iterator::peer() const { return it.just()->peer; }

bool
beaconclient::iterator::finished() const { return it.just().finished(); }

void
beaconclient::iterator::next() { it.just().next(); }

beaconclient::iterator::~iterator() { content.flush(); }

beaconclient::iterator
beaconclient::start(maybe<interfacetype> type) const {
    if (type != Nothing && config.type() != Nothing && config.type() != type) {
        logmsg(loglevel::error,
               "request for peers of type " + fields::mk(type) +
               " on a client which only tracks those of type " +
               fields::mk(config.type())); }
    return iterator(*this, type); }

const publisher &
beaconclient::changed() const { return _changed; }

void
beaconclient::status(loglevel level) const {
    cachelock.locked([this, level] (mutex_t::token tok) {
            for (auto it(cache.start()); !it.finished(); it.next()) {
                it->status(tok, level); } }); }

void
beaconclient::destroy() {
    shutdown.set();
    /* Guaranteed to be quick because shutdown is set. */
    join(clientio::CLIENTIO); }

beaconclient::~beaconclient() { }
