#include "rpcserver.H"

#include "fields.H"
#include "logging.H"
#include "rpcconn.H"
#include "test.H"
#include "timedelta.H"

#include "list.tmpl"
#include "test.tmpl"
#include "wireproto.tmpl"

void
rpcserver::run(clientio io) {
    list<subscription> deathsubs;
    subscriber sub;
    subscription shutdownsub(sub, shutdown.pub);
    iosubscription ios(sub, sock.poll());

    while (!shutdown.ready() || !deathsubs.empty()) {
        auto s = sub.wait(io);
        if (s == &shutdownsub) {
            shutdownsub.detach();
            for (auto it(deathsubs.start()); !it.finished(); it.next()) {
                auto c(static_cast<rpcconn *>(it->data));
                c->teardown(); }
        } else if (s == &ios) {
            /* Accept a new incoming connection */
            tests::rpcserver::accepting.trigger();
            /* Should be quick because ios was notified. */
            auto newsock(sock.accept(io));
            tests::rpcserver::accepted.trigger(newsock);
            ios.rearm();
            if (newsock.isfailure()) {
                newsock.failure().warn("accepting incoming connection");
                /* Back off a little so that networking problems don't
                   completely screw us.  We're probably still dead,
                   but at least this way we die in a way which doesn't
                   spam the logs with crap. */
                (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
                continue; }
            auto conn(accept(newsock.success()));
            if (conn.isfailure()) {
                conn.failure().warn(
                    "derived class rejected incoming connection");
                newsock.success().close();
                continue; }
            deathsubs.append(sub, conn.success()->pub(), conn.success());
        } else {
            /* Must have been a connection sub. */
            auto conn(static_cast<rpcconn *>(s->data));
            auto death(conn->hasdied());
            if (!death) continue;
            /* Connection has died.  Finish tearing it down. */
            for (auto it(deathsubs.start()); true; it.next()) {
                if (&*it == s) {
                    it.remove();
                    break; } }
            conn->join(death.just()); } }
    finish(io); }

rpcserver::rpcserver(constoken t, listenfd fd)
    : thread(t),
      shutdown(),
      sock(fd) {}

rpcserver::status_t
rpcserver::status() const {
    return status_t(sock.status()); }

rpcserverstatus::rpcserverstatus(quickcheck q)
    : fd(q) {}

bool
rpcserverstatus::operator==(const rpcserverstatus &o) const {
    return o.fd == fd; }

wireproto_simple_wrapper_type(rpcserverstatus,
                              listenfd::status_t,
                              fd)

void
rpcserver::destroy(clientio io) {
    /* Test harness can set this early, so don't double-set it here. */
    if (!shutdown.ready()) shutdown.set(true);
    auto s(sock);
    join(io);
    s.close(); }

void
rpcserver::finish(clientio) {}

const fields::field &
fields::mk(const rpcserverstatus &s) {
    return "<rpcserverstatus: " + mk(s.fd) + ">"; }

tests::event<void> tests::rpcserver::accepting;
tests::event<orerror<socket_t> &> tests::rpcserver::accepted;
