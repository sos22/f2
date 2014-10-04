#include "controlserver.H"

#include "buildconfig.H"
#include "logging.H"
#include "peername.H"
#include "proto.H"
#include "shutdown.H"

#include "list.tmpl"
#include "rpcservice.tmpl"

class controlinvoker {
public: list<controlinterface *> inner;
};

controlinterface::controlinterface(controlserver *cs)
    : owner(cs),
      pendinginvoke(),
      refcount(0),
      idle(),
      started(false) { }

void
controlinterface::start() {
    started = true;
    owner->ifacelock.locked([this] (mutex_t::token) {
            owner->ifaces.pushtail(this); }); }

controlinterface::~controlinterface() {
    assert(started);
    auto token(owner->ifacelock.lock());
    /* Prevent anyone else from adding us to their invoking list. */
    for (auto it(owner->ifaces.start()); true; it.next()) {
        if (*it == this) {
            it.remove();
            break; } }
    /* Pull ourselves out of the pending invoke lists.  We won't be
     * re-added because this->inuse is clear. */
    for (auto it(pendinginvoke.start()); !it.finished(); it.next()) {
        for (auto it2((*it)->inner.start()); true; it2.next()) {
            if (*it2 == this) {
                refcount--;
                it2.remove();
                break; } } }
    /* Wait for any remaining references to drop away.  No more can be
     * created because we're not in the pending invoke lists.  This
     * will only wait as long as the getstatus() and getlistening()
     * methods do, so since they don't have a clientio token this
     * doesn't need one either. */
    if (refcount != 0) {
        subscriber sub;
        subscription ss(sub, idle);
        unsigned lastref = refcount;
        while (refcount != 0) {
            assert(refcount <= lastref);
            owner->ifacelock.unlock(&token);
            sub.wait(clientio::CLIENTIO);
            token = owner->ifacelock.lock();
            lastref = refcount; } }
    owner->ifacelock.unlock(&token); }

controlserver::controlserver(const constoken &token,
                             waitbox<shutdowncode> &_shutdown)
    : rpcservice(token),
      ifaces(),
      ifacelock(),
      shutdown(_shutdown),
      shutdownlock() {}

void
controlserver::invoke(const std::function<void (controlinterface *)> &f) {
    /* Complicated two-step lookup so that you can always unregister
       interface A without having to wait for operations on interface
       B to complete. */
    controlinvoker ci;
    auto token(ifacelock.lock());
    for (auto it(ifaces.start()); !it.finished(); it.next()) {
        auto iface(*it);
        iface->pendinginvoke.pushtail(&ci);
        ci.inner.pushtail(iface); }
    while (!ci.inner.empty()) {
        auto iface(ci.inner.pophead());
        iface->refcount++;
        assert(iface->refcount != 0);
        for (auto it(iface->pendinginvoke.start()); true; it.next()) {
            if (*it == &ci) {
                it.remove();
                break; } }
        /* We've bumped refcount, so can drop the lock without risk of
           the interface disappearing underneath us. */
        ifacelock.unlock(&token);

        /* Actually invoke the desired callback. */
        f(iface);

        token = ifacelock.lock();
        assert(iface->refcount > 0);
        iface->refcount--;
        if (iface->refcount == 0 && iface->pendinginvoke.empty()) {
            iface->idle.publish(); } }
    ifacelock.unlock(&token); }

void
controlserver::call(const wireproto::rx_message &rxm, response *resp) {
    if (rxm.tag() == proto::QUIT::tag) {
        auto reason(rxm.getparam(proto::QUIT::req::reason));
        auto msg(rxm.getparam(proto::QUIT::req::message));
        if (!reason || !msg) {
            resp->fail(error::missingparameter);
            return; }
        if (shutdownlock.locked<bool>([this, &reason] (mutex_t::token) {
                    if (shutdown.ready()) return false;
                    else {
                        shutdown.set(reason.just());
                        return true; } } ) ) {
            logmsg(loglevel::notice,
                   "received a quit message: " + fields::mk(msg.just()));
            resp->complete(); }
        else {
            logmsg(loglevel::notice,
                   "reject too-late shutdown " + fields::mk(msg.just()));
            resp->fail(error::toolate); } }
    else if (rxm.tag() == proto::STATUS::tag) {
        resp->addparam(proto::STATUS::resp::controlserver, status());
        invoke([resp] (controlinterface *si) { si->getstatus(resp); });
        resp->complete(); }
    else if (rxm.tag() == proto::GETLOGS::tag) {
        auto r(::getlogs(rxm, resp));
        if (r.isfailure()) resp->fail(r.failure());
        else resp->complete(); }
    else if (rxm.tag() == proto::BUILDCONFIG::tag) {
        resp->addparam(proto::BUILDCONFIG::resp::config, buildconfig::us);
        resp->complete();}
    else if (rxm.tag() == proto::LISTENING::tag) {
        resp->addparam(proto::LISTENING::resp::control, localname());
        invoke([resp] (controlinterface *si) { si->getlistening(resp); });
        resp->complete(); }
    else resp->fail(error::unrecognisedmessage); }

orerror<controlserver *>
controlserver::build(clientio io, const peername &p, waitbox<shutdowncode> &s) {
    return rpcservice::listen<controlserver>(io, p, s); }
