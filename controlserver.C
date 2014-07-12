#include "controlserver.H"

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

class controlconn : public rpcconn {
public: controlserver *owner;
public: controlconn(socket_t _sock,
                    const peername &_peer)
    : rpcconn(_sock, _peer),
      owner(NULL) {}
public: messageresult message(const wireproto::rx_message &);
public: ~controlconn() {}
};

statusinterface::statusinterface(controlserver *cs)
    : active(),
      started(false),
      running(false),
      idle(),
      owner(cs) {}

void
statusinterface::start() {
    auto token(owner->statuslock.lock());
    assert(!started);
    started = true;
    owner->statusifaces.pushtail(this);
    owner->statuslock.unlock(&token); }

void
statusinterface::stop() {
    auto token(owner->statuslock.lock());
    assert(started);
    /* Clearing started prevents the interface from being added to the
     * active list. */
    started = false;
    for (auto it(owner->statusifaces.start()); !it.finished(); it.next()) {
        if (*it == this) {
            it.remove();
            break; } }
    /* Removing the interface from the active list prevents running
     * from being set. */
    assert(!!active.next == !!active.prev);
    if (active.next) {
        active.next->prev = active.prev;
        active.prev->next = active.next; }
    owner->statuslock.unlock(&token);
    if (loadacquire(running)) {
        subscriber sub;
        subscription ss(sub, idle);
        while (loadacquire(running)) sub.wait(); }
    /* Running is false, so the callback is no longer running, and the
       interface is no longer in the registered or active lists, so
       we're done. */}

statusinterface::~statusinterface() {
    assert(!started);
    assert(!running);
    assert(!active.next);
    assert(!active.prev); }

messageresult
controlconn::message(const wireproto::rx_message &rxm) {
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
                   "reject too-late shutdown from " + fields::mk(peer())); }
        return messageresult::noreply;
    } else if (rxm.tag() == proto::STATUS::tag) {
        auto res(new wireproto::resp_message(rxm));
        statusinterface::list ll;
        ll.next = &ll;
        ll.prev = &ll;
        auto token(owner->statuslock.lock());
        for (auto it(owner->statusifaces.start());
             !it.finished();
             it.next()) {
            auto iface(*it);
            if (!iface->started) continue;
            iface->active.next = &ll;
            iface->active.prev = ll.prev;
            iface->active.prev->next = &iface->active;
            iface->active.next->prev = &iface->active; }
        owner->statuslock.unlock(&token);
        for (auto cursor = ll.next; cursor != &ll; cursor = ll.next) {
            auto iface = containerof(ll.next, statusinterface, active);
            token = owner->statuslock.lock();
            /* Remove from active list.  Either it's stopped, in which
               case we need to ignore it, or it's not, and we'll rely
               on setting the running flag to stop it from going
               away. */
            iface->active.next->prev = iface->active.prev;
            iface->active.prev->next = iface->active.next;
            iface->active.next = NULL;
            iface->active.prev = NULL;
            if (!iface->started) {
                /* Interface has been stopped -> we can't mark it as
                 * running. */
                owner->statuslock.unlock(&token);
                continue; }
            /* Tell anyone trying to unregister the interface that
               they're going to have to wait. */
            iface->running = true;
            owner->statuslock.unlock(&token);
            iface->getstatus(res);
            storerelease(&iface->running, false);
            iface->idle.publish(); }
        assert(ll.prev == &ll);
        return res;
    } else if (rxm.tag() == proto::GETLOGS::tag) {
        return getlogs(rxm);
    } else {
        return rpcconn::message(rxm); } }

controlserver::controlserver(waitbox<shutdowncode> &_shutdown)
    : shutdown(_shutdown) {}

orerror<controlconn *>
controlserver::accept(socket_t s) {
    auto r(rpcconn::fromsocket<controlconn>(s));
    if (r.issuccess()) {
        /* We don't need a HELLO because we only listen on UNIX domain
           sockets, which are implicitly authenticated by the socket
           access flags. */
        /* Set flag now, before rpcserver() calls conn::ready(), to
           avoid silly startup races. */
        r.success()->receivedhello = true;
        r.success()->owner = this; }
    return r; }

orerror<controlserver *>
controlserver::build(const peername &p, waitbox<shutdowncode> &s)
{
    auto r(new controlserver(s));
    auto e(r->listen(p));
    if (e.isnothing()) {
        return r;
    } else {
        delete r;
        return e.just(); } }
