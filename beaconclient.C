#include "beaconclient.H"

#include "beacon.H"
#include "logging.H"
#include "proto.H"
#include "tuple.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "mutex.tmpl"
#include "parsers.tmpl"
#include "thread.tmpl"
#include "wireproto.tmpl"

#include "fieldfinal.H"

beaconclientconfig
beaconclientconfig::dflt(const clustername &_cluster,
                         maybe<actortype> _type,
                         const maybe<slavename> &_name) {
    return beaconclientconfig(beaconconfig::dflt,
                              _cluster,
                              _type,
                              _name,
                              timedelta::seconds(1),
                              timedelta::seconds(120),
                              timedelta::seconds(300)); }
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
            ~(" gctimeout:" + _timedelta()) +
            ">")
        .map<beaconclientconfig>(
            []
            (const pair<pair<pair<pair<pair<pair<maybe<beaconconfig>,
                                                 clustername>,
                                            maybe<actortype> >,
                                       maybe<slavename> >,
                                  maybe<timedelta> >,
                             maybe<timedelta> >,
                        maybe<timedelta> > &x) {
                return beaconclientconfig(
                    x.first().first().first().first().first().first().dflt(
                        beaconconfig::dflt),
                    x.first().first().first().first().first().second(),
                    x.first().first().first().first().second(),
                    x.first().first().first().second(),
                    x.first().first().second().dflt(timedelta::seconds(1)),
                    x.first().second().dflt(timedelta::seconds(60)),
                    x.second().dflt(timedelta::seconds(300))); }); }

beaconclientslot::beaconclientslot(const slavename &_name,
                                   walltime whenwall,
                                   timestamp when)
    : name(_name),
      result(Nothing),
      lastbeaconrequestwall(Nothing),
      lastusedwall(whenwall),
      refcount(1),
      lastbeaconrequest(Nothing),
      lastused(when),
      dead(false),
      expiry(Nothing),
      lastbeaconresponse(Nothing),
      mux(),
      pub() {}

beaconclientslot::beaconclientslot(const slavename &sn,
                                   const beaconclientresult &bcr,
                                   walltime whenwall,
                                   timestamp when,
                                   timestamp _expiry)
    : name(sn),
      result(bcr),
      lastbeaconrequestwall(Nothing),
      lastusedwall(whenwall),
      refcount(1),
      lastbeaconrequest(Nothing),
      lastused(when),
      dead(false),
      expiry(_expiry),
      lastbeaconresponse(when),
      mux(),
      pub() {}

void
beaconclientslot::ref() {
    mux.locked([this] (mutex_t::token) { refcount++; }); }

void
beaconclientslot::deref() {
    if (mux.locked<bool>([this] (mutex_t::token) {
                refcount--;
                return refcount == 0; })) {
        delete this; } }

beaconclientslot::status_t
beaconclientslot::status() const {
    /* Note that some fields can be changed without holding the lock.
       That should be fine; they're all sufficiently simple that
       getting inconsistent copies won't cause a crash, and slightly
       odd results are fine for a debug interface. */
    return mux.locked<status_t>([this] (mutex_t::token) {
            return status_t(name,
                            result,
                            lastbeaconrequestwall,
                            lastusedwall,
                            refcount); } ); }

beaconclientstatus::beaconclientstatus(
    const beaconclientconfig &_config,
    int _errors)
    : config(_config),
      cache(),
      errors(_errors) {}

beaconclientstatus::~beaconclientstatus() {
    cache.flush(); }

beaconclient::controliface::controliface(beaconclient *_owner,
                                         controlserver *cs)
    : controlinterface(cs),
      owner(_owner) {}

void
beaconclient::controliface::getstatus(wireproto::tx_message *msg) const {
    msg->addparam(proto::STATUS::resp::beaconclient, owner->status()); }

void
beaconclient::controliface::getlistening(wireproto::resp_message *) const { }

beaconclient::beaconclient(const thread::constoken &token,
                           const beaconclient::config_t &_config,
                           controlserver *cs,
                           udpsocket _listenfd,
                           udpsocket _clientfd)
    : thread(token),
      config(_config),
      _controliface(Nothing),
      listenfd(_listenfd),
      clientfd(_clientfd),
      errors(0),
      ignored(0) {
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
                    auto entry(*it);
                    nextactivity =
                        min(nextactivity, entry->lastused + config.gctimeout);
                    if (entry->result == Nothing) {
                        /* Don't know where to send this one to -> next
                         * query should be a broadcast. */
                        broadcast = true;
                        if (entry->lastbeaconrequest == Nothing) {
                            /* No knowledge and no outstanding request
                             * -> send it now. */
                            nextactivity = timestamp::now();
                            break; } }
                    if (entry->lastbeaconrequest != Nothing) {
                        /* Waiting for a request to come back with a
                         * response -> resend after a short
                         * timeout. */
                        nextactivity = min(
                            nextactivity,
                            entry->lastbeaconrequest.just() +
                                config.queryinterval); }
                    else {
                        /* No outstanding requests -> start one once
                         * we're halfway through the existing entry's
                         * expiry time. */
                        nextactivity = min(
                            nextactivity,
                            entry->expiry.just() +
                            (entry->expiry.just() -
                             entry->lastbeaconresponse.just())
                                * .5); } } });
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
                logmsg(loglevel::info,
                       "unexpected message tag " +
                       fields::mk(msg.tag()) +
                       " from " +
                       fields::mk(rr.success()) +
                       " on beacon response port");
                errors++;
                continue; }
            auto respversion(msg.getparam(proto::BEACON::resp::version));
            if (respversion != 1u) {
                logmsg(loglevel::info,
                       "beacon response version " +
                       fields::mk(respversion) +
                       " from " +
                       fields::mk(rr.success()) +
                       "; expected version 1");
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
                        auto entry(*it);
                        if (entry->name == respname) {
                            entry->mux.locked([&bcr, entry] (mutex_t::token) {
                                    entry->result = bcr; });
                            entry->pub.publish();
                            found = true;
                            break; } }
                    if (!found) {
                        /* New peer which we've never heard of
                         * before. */
                        cache.pushtail(
                            new beaconclientslot(
                                respname.just(),
                                bcr,
                                walltime::now(),
                                timestamp::now(),
                                respcachetime.just() + timestamp::now())); } });
            continue; }
        /* Must have hit the timeout.  Figure out what we're doing
         * about it. */
        assert(notified == NULL);
        if (timestamp::now() > nextbroadcast) broadcast = true;
        if (broadcast) {
            /* Either it's time for a maintenance broadcast or we need
             * an address for a currently-unknown peer.  In either
             * case, we have to send a broadcast query. */
            wireproto::tx_message msg(proto::BEACON::req::tag);
            msg.addparam(proto::BEACON::req::version, 1u);
            msg.addparam(proto::BEACON::req::cluster, config.cluster);
            if (config.name != Nothing) {
                msg.addparam(proto::BEACON::req::name, config.name.just()); }
            if (config.type != Nothing) {
                msg.addparam(proto::BEACON::req::type, config.type.just()); }
            buffer txbuf;
            msg.serialise(txbuf);
            auto r(clientfd.send(txbuf,
                                 peername::udpbroadcast(config.proto.reqport)));
            if (r.isfailure()) {
                r.failure().warn("sending beacon broadcast request");
                errors++; }
            nextbroadcast = timestamp::now() + config.broadcastinterval; }
        cachelock.locked([this, broadcast] (mutex_t::token) {
            for (auto it(cache.start()); !it.finished(); ) {
                auto entry(*it);
                entry->mux.locked(
                    [broadcast, entry, &it, this]
                    (mutex_t::token) {
                    if (entry->lastused + config.gctimeout < timestamp::now()) {
                        /* Entry expired */
                        it.remove();
                        entry->mux.locked([entry] (mutex_t::token) {
                                entry->dead = true; });
                        entry->pub.publish();
                        entry->deref(); }
                    else if (broadcast) {
                        /* Sending a broadcast counts as a query on
                         * all peers. */
                        entry->lastbeaconrequest = timestamp::now(); }
                    else if (entry->result == Nothing) {
                        /* Can't send directed request; don't know
                         * where to send it to.  Wait for broadcasts
                         * to do their thing. */
                        /* Most of the time, ignorant entries will set
                         * the broadcast slot, so we won't get here,
                         * but it's possible that we might sometimes
                         * if we race with someone querying for a
                         * fresh slave from another thread.  Just go
                         * around again; we'll broadcast next time. */ }
                    else {
                        auto deadline =
                            entry->lastbeaconrequest == Nothing
                            ? (/* No outstanding queries -> do it once
                                  we're halfway through the cache
                                  validity timeout. */
                                entry->lastbeaconresponse.just() +
                                (entry->expiry.just() -
                                 entry->lastbeaconresponse.just()) * .5)
                            : (/* Outstanding query -> waiting for
                                  peer to respond. */
                                entry->lastbeaconrequest.just() +
                                config.queryinterval);
                        if (timestamp::now() > deadline) {
                            /* Time to send another beacon request for
                               this slave.  We do this under the lock,
                               which is just about OK because our UDP
                               sockets drop rather than blocking. */
                            buffer txbuf;
                            wireproto::tx_message(proto::BEACON::req::tag)
                                .addparam(proto::BEACON::req::version, 1u)
                                .addparam(proto::BEACON::req::cluster,
                                          config.cluster)
                                .addparam(proto::BEACON::req::name,
                                          entry->name)
                                .addparam(proto::BEACON::req::type,
                                          entry->result.just().type)
                                .serialise(txbuf);
                            auto r(clientfd.send(txbuf,
                                                 entry->result.just().beacon));
                            if (r.isfailure()) {
                                r.warn("sending beacon directed request");
                                errors++; }
                            /* Ignore errors, apart from logging them;
                               they'll be covered by the usual retry
                               logic anyway. */
                            entry->lastbeaconrequest =
                                timestamp::now(); } } } ); } }); }
    /* Control interface is still runnng so this has to be under the
     * cache lock. */
    cachelock.locked([this] (mutex_t::token) { cache.flush(); });
    listenfd.close();
    clientfd.close(); }

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
        for (auto it2(what->cache.start()); !it2.finished(); it2.next()) {
            auto entry2(*it2);
            entry2->mux.locked([this, entry2, _type] (mutex_t::token) {
                if (entry2->result != Nothing) {
                    auto &r(entry2->result.just());
                    if (_type == Nothing || _type == r.type) {
                        content.append(entry2->name,
                                       r.type,
                                       r.server); } } } ); } } );
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

beaconclient::status_t
beaconclient::status() const {
    status_t res(config, errors);
    cachelock.locked([this, &res] (mutex_t::token) {
        for (auto it(cache.start()); !it.finished(); it.next()) {
            res.cache.append((*it)->status()); } });
    return res; }

void
beaconclient::destroy(clientio io) {
    shutdown.set();
    join(io); }

beaconclient::~beaconclient() { }
