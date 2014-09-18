#include "controlserver.H"

#include "buildconfig.H"
#include "fields.H"
#include "logging.H"
#include "proto.H"
#include "rpcconn.H"
#include "shutdown.H"
#include "util.H"

#include "list.tmpl"
#include "rpcconn.tmpl"
#include "rpcserver.tmpl"

/* Cast to 1 rather than NULL to avoid stupid compiler crap. */
#define containerof(thing, type, field)                                 \
    ({  type *__res = (type *)((unsigned long)thing + 1-                \
                               (unsigned long)&((type *)1)->field);     \
        (void)(&__res->field == (thing));                               \
        __res;                                                          \
    })

controlinterface::controlinterface(controlserver *cs)
    : inuse(true),
      invoking(),
      active(0),
      idle(),
      owner(cs) {
    owner->ifacelock.locked([this] (mutex_t::token) {
            owner->ifaces.pushtail(this); }); }

controlinterface::~controlinterface() {
    auto token(owner->ifacelock.lock());
    /* Prevent anyone else from adding us to their invoking list. */
    inuse = false;
    /* Remove ourselves from all of the invoking lists. */
    while (!invoking.empty()) {
        auto conn(invoking.pophead());
        bool found = false;
        for (auto it2(conn->invoking.start());
             !it2.finished() && !found;
             it2.next()) {
            if (*it2 == this) {
                it2.remove();
                found = true; } }
        assert(found); }
    /* Wait for any conns which have already started invoking us to
     * finish. */
    if (active != 0) {
        subscriber sub;
        subscription ss(sub, idle);
        while (active != 0) {
            owner->ifacelock.unlock(&token);
            /* running will be cleared when the getstatus() invocation
               finishes.  getstatus() does not receive a clientio
               token, so will complete quickly, and we do not need a
               token for the wait here. */
            sub.wait(clientio::CLIENTIO);
            token = owner->ifacelock.lock(); } }
    assert(invoking.empty());
    /* Remove ourselves from the owner's iface list.  Once we've done
     * this and dropped the owner ifacelock the owner can be released
     * at any time. */
    for (auto it(owner->ifaces.start()); !it.finished(); it.next()) {
        if (*it == this) {
            it.remove();
            break; } }
    owner->ifacelock.unlock(&token); }

controlconn::controlconn(const rpcconn::rpcconntoken &tok,
                         controlserver *_owner)
    : rpcconn(tok),
      invoking(),
      owner(_owner) {}

void
controlconn::invoke(const std::function<void (controlinterface *)> &f) {
    /* Complicated two-step lookup so that you can always unregister
       interface A without having to wait for operations on interface
       B to complete. */
    auto token(owner->ifacelock.lock());
    assert(invoking.empty());
    for (auto it(owner->ifaces.start()); !it.finished(); it.next()) {
        auto iface(*it);
        if (iface->inuse) {
            iface->invoking.pushtail(this);
            invoking.pushtail(iface); } }
    while (!invoking.empty()) {
        auto iface(invoking.pophead());
        assert(iface->inuse);
        iface->active++;
        assert(iface->active != 0);
        bool found = false;
        for (auto it(iface->invoking.start()); !it.finished(); it.next()) {
            if (*it == this) {
                it.remove();
                found = true;
                break; } }
        assert(found);
        /* We've bumped active, so can drop the lock without risk of
           the interface disappearing underneath us. */
        owner->ifacelock.unlock(&token);

        /* Actually invoke the desired callback. */
        f(iface);

        token = owner->ifacelock.lock();
        assert(iface->active > 0);
        iface->active--;
        if (iface->active == 0 && iface->invoking.empty()) {
            iface->idle.publish(); } }
    owner->ifacelock.unlock(&token); }

rpcconn::messageresult
controlconn::message(const wireproto::rx_message &rxm, messagetoken token) {
    auto res(new wireproto::resp_message(rxm));
    if (rxm.tag() == proto::QUIT::tag) {
        auto reason(rxm.getparam(proto::QUIT::req::reason));
        auto msg(rxm.getparam(proto::QUIT::req::message));
        if (!reason || !msg) return error::missingparameter;
        if (!owner->shutdown.ready()) {
            logmsg(loglevel::notice,
                   "received a quit message: " + fields::mk(msg.just()) +
                   " from " + fields::mk(peer()));
            owner->shutdown.set(reason.just());
        } else {
            logmsg(loglevel::notice,
                   "reject too-late shutdown from " + fields::mk(peer())); } }
    else if (rxm.tag() == proto::STATUS::tag) {
        invoke([res] (controlinterface *si) { si->getstatus(res); }); }
    else if (rxm.tag() == proto::GETLOGS::tag) {
        auto r(::getlogs(rxm, res));
        if (r.isfailure()) {
            delete res;
            return r.failure(); } }
    else if (rxm.tag() == proto::BUILDCONFIG::tag) {
        res->addparam(proto::BUILDCONFIG::resp::config, buildconfig::us); }
    else if (rxm.tag() == proto::LISTENING::tag) {
        res->addparam(proto::LISTENING::resp::control, owner->localname());
        invoke([res] (controlinterface *si) { si->getlistening(res); }); }
    else {
        delete res;
        return rpcconn::message(rxm, token); }
    return res; }

controlserver::controlserver(constoken token,
                             listenfd fd,
                             waitbox<shutdowncode> &_shutdown)
    : rpcserver(token, fd),
      shutdown(_shutdown) {}

orerror<rpcconn *>
controlserver::accept(socket_t s) {
    return rpcconn::fromsocket<controlconn>(
        s,
        rpcconnconfig::dflt,
        this); }

orerror<controlserver *>
controlserver::build(const peername &p, waitbox<shutdowncode> &s) {
    return rpcserver::listen<controlserver>(p, s)
        .map<controlserver *>([] (pausedrpcserver<controlserver> ss) {
                return ss.go(); }); }
