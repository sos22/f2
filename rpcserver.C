#include "rpcserver.H"

#include "fields.H"
#include "rpcconn.H"

#include "list.tmpl"

namespace _rpcserver {
class connsub : public subscription {
    public: rpcconn *conn;
    public: connsub(subscriber &_sub, rpcconn *_conn);
};
}

_rpcserver::connsub::connsub(subscriber &_sub, rpcconn *_conn)
    : subscription(_sub, _conn->deathpublisher()),
      conn(_conn) {
    if (_conn->hasdied() != Nothing) set(); }

void
rpcserver::run(clientio io) {
    list<_rpcserver::connsub *> threads;
    subscriber sub;
    subscription shutdownsub(sub, shutdown.pub);
    iosubscription ios(io, sub, sock.poll());

    while (!shutdown.ready() || !threads.empty()) {
        auto s = sub.wait();
        if (s == &shutdownsub) {
            shutdownsub.detach();
            for (auto it(threads.start()); !it.finished(); it.next()) {
                (*it)->conn->teardown(); }
        } else if (s == &ios) {
            /* Accept a new incoming connection */
            auto newsock(sock.accept());
            ios.rearm();
            if (newsock.isfailure()) {
                newsock.failure().warn("accepting incoming connection");
                continue; }
            if (shutdown.ready()) {
                newsock.success().close();
                /* Don't rearm, so we don't try and accept any more
                 * connections. */
                continue; }
            auto conn(accept(newsock.success()));
            if (conn.isfailure()) {
                conn.failure().warn(
                    "derived class rejected incoming connection");
                newsock.success().close();
                continue; }
            threads.pushtail(new _rpcserver::connsub(sub, conn.success()));
        } else {
            /* Must have been a connection sub. */
            auto cs(static_cast<_rpcserver::connsub *>(s));
            auto death(cs->conn->hasdied());
            if (!death) continue;
            /* Connection has died.  Finish tearing it down. */
            bool found = false;
            for (auto it(threads.start()); !it.finished(); it.next()) {
                if (*it == cs) {
                    found = true;
                    it.remove();
                    break; } }
            assert(found);
            rpcconn *conn = cs->conn;
            delete cs;
            conn->destroy(death.just()); } } }

rpcserver::rpcserver()
    : thr(NULL),
      shutdown(),
      sock() {}

orerror<void>
rpcserver::listen(const peername &p) {
    auto s(socket_t::listen(p));
    if (s.isfailure()) return s.failure();
    sock = s.success();
    return thread::spawn(this, &thr, "root " + fields::mk(p))
        .iffailed([&sock] () { sock.close();}); }

void
rpcserver::destroy(clientio io) {
    assert(thr);
    shutdown.set(true);
    thr->join(io);
    thr = NULL;
    sock.close();
    delete this; }

rpcserver::~rpcserver() {
    assert(thr == NULL); }
