#include "rpctest.H"

#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "logging.H"
#include "peername.H"
#include "proto.H"
#include "rpcclient.H"
#include "rpcservice.H"
#include "spark.H"
#include "test.H"
#include "timedelta.H"

#include "list.tmpl"
#include "rpcservice.tmpl"
#include "spark.tmpl"

namespace tests {

class trivserver : public rpcservice {
public:  trivserver(const rpcservice::constoken &t)
    : rpcservice(t) {}
private: void call(const wireproto::rx_message &, response *resp) {
    resp->fail(error::unrecognisedmessage); } };

class slowserver : public rpcservice {
private: const timedelta mindelay;
private: const timedelta maxdelay;
private: list<spark<void> > running;
public:  slowserver(const constoken &t,
                    timedelta _mindelay,
                    timedelta _maxdelay)
    : rpcservice(t),
      mindelay(_mindelay),
      maxdelay(_maxdelay) {}
public:  void call(const wireproto::rx_message &, response *resp) {
    running.append([resp, this] {
            (timestamp::now() + timedelta(quickcheck(), mindelay, maxdelay))
                .sleep(clientio::CLIENTIO);
            resp->complete(); }); }
public:  void destroying(clientio) { running.flush(); }
private: ~slowserver() { assert(running.empty()); } };

void _rpc() {
    testcaseIO("rpc", "basic", [] (clientio io) {
            auto s(rpcservice::listen<trivserver>(
                       peername::loopback(peername::port::any))
                   .fatal("starting trivial server"));
            auto r(s->localname());
            auto c(rpcclient::connect(io, r)
                   .fatal("connecting to trivial server"));
            delete c->call(io, wireproto::req_message(proto::PING::tag))
                .fatal("pinging trivial server");
            assert(c->call(io, wireproto::req_message(wireproto::msgtag(93)))
                   == error::unrecognisedmessage);
            auto c2(rpcclient::connect(io, r)
                   .fatal("connecting to trivial server"));
            delete c2->call(io, wireproto::req_message(proto::PING::tag))
                .fatal("pinging trivial server");
            assert(c2->call(io, wireproto::req_message(wireproto::msgtag(93)))
                   == error::unrecognisedmessage);
            delete c;
            (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
            delete c2;
            s->destroy(io);
            assert(rpcclient::connect(io, r) ==
                   error::from_errno(ECONNREFUSED)); });
    testcaseIO("rpc", "listenbad", [] (clientio io) {
            auto s(rpcservice::listen<trivserver>(
                       peername::loopback(peername::port::any))
                   .fatal("starting trivial server"));
            assert(rpcservice::listen<trivserver>(
                       peername::loopback(s->localname().getport())) ==
                   error::from_errno(EADDRINUSE));
            s->destroy(io); });
    testcaseIO("rpc", "connectbad", [] (clientio io) {
            struct sockaddr sa;
            sa.sa_family = 9999;
            peername p(&sa, sizeof(sa));
            assert(rpcclient::connect(io, p) ==
                   error::from_errno(EAFNOSUPPORT));
            assert(rpcclient::connect(
                       io, peername::udpbroadcast(peername::port(73))) ==
                   error::from_errno(ENETUNREACH)); });
    testcaseIO("rpc", "connectshutdown", [] (clientio io) {
            /* Try to arrange to interrupt a connect before it
             * completes.  1000 iterations should be enough to have a
             * decent chance of success. */
            auto s(rpcservice::listen<trivserver>(
                       peername::loopback(peername::port::any))
                   .fatal("starting trivial server"));
            auto p(s->localname());
            for (unsigned x = 0; x < 1000; x++) {
                rpcclient::connect(p)->abort(); }
            s->destroy(io); });
    testcaseIO("rpc", "slowcalls", [] (clientio io) {
            for (unsigned mode = 0; mode < 3; mode++) {
                auto s(rpcservice::listen<slowserver>(
                           peername::loopback(peername::port::any),
                           timedelta::milliseconds(0),
                           timedelta::milliseconds(100))
                       .fatal("starting slow server"));
                auto p(s->localname());
                waitbox<void> shutdown;
                list<spark<void> > workers;
                unsigned completed = 0;
                for (unsigned x = 0; x < 10; x++) {
                    workers.append([&completed, io, mode, &p, &shutdown] {
                            auto c(rpcclient::connect(io, p)
                                   .fatal("connecting to slow server"));
                            list<rpcclient::asynccall *> calls;
                            while (!shutdown.ready()) {
                                while (calls.length() < 10) {
                                    calls.pushtail(
                                        c->call(wireproto::req_message(
                                                    wireproto::msgtag(99)))); }
                                auto cc(calls.pophead());
                                {   subscriber sub;
                                    subscription ss(sub, cc->pub());
                                    subscription sss(sub, shutdown.pub);
                                    while (!shutdown.ready() &&
                                           cc->finished() == Nothing) {
                                        sub.wait(io); } }
                                if (shutdown.ready()) cc->abort();
                                else {
                                    auto r(cc->pop(cc->finished().just()));
                                    if (r.issuccess()) {
                                        completed++;
                                        delete r.success(); }
                                    else if (mode == 0 ||
                                             r != error::disconnected)  {
                                        r.fatal("finishing slow call"); } } }
                            while (!calls.empty()) calls.pophead()->abort();
                            delete c; } ); }
                (timestamp::now() + timedelta::seconds(1)).sleep(io);
                ::logmsg(loglevel::info,
                         "completed " + fields::mk(completed) + " in mode " +
                         fields::mk(mode));
                if (mode == 2) s->destroy(io);
                shutdown.set();
                if (mode == 1) s->destroy(io);
                workers.flush();
                if (mode == 0) s->destroy(io); } } );
    testcaseIO("rpc", "outoffds", [] (clientio io) {
            /* Reduce the FD limit and deliberately run into it, to
             * make sure that we recover when expected. */
            auto s(rpcservice::listen<trivserver>(
                       peername::loopback(peername::port::any))
                   .fatal("starting trivial server"));
            struct rlimit oldlimit;
            if (::getrlimit(RLIMIT_NOFILE, &oldlimit) < 0) {
                error::from_errno().fatal("getting rlimit NOFILE"); }
            struct rlimit newlimit(oldlimit);
            newlimit.rlim_cur = 30;
            if (::setrlimit(RLIMIT_NOFILE, &newlimit) < 0) {
                error::from_errno().fatal("setting rlimit NOFILE"); }
            list<rpcclient *> clients;
            mutex_t lock;
            waitbox<void> firsttimeout;
            waitbox<void> restart;
            waitbox<void> progress;
            spark<void> connector(
                [&clients, &firsttimeout, io, &lock, &restart, &progress, s] {
                    int phase = 0;
                    for (unsigned x = 0; x < 30; x++) {
                        auto deadline(timestamp::now() +
                                      timedelta::milliseconds(100));
                        auto c(rpcclient::connect(s->localname()));
                        subscriber sub;
                        subscription ss(sub, c->pub());
                        while (true) {
                            auto t(c->finished());
                            if (t.isjust()) {
                                lock.locked([&clients, c, t] (mutex_t::token) {
                                        clients.pushtail(
                                            c->pop(t.just())
                                            .fatal("connecting")); } );
                                if (phase == 1) {
                                    progress.set();
                                    phase = 2; }
                                break; }
                            auto scr(sub.wait(io, deadline));
                            if (scr == &ss) continue;
                            assert(scr == NULL);
                            if (phase == 0) {
                                firsttimeout.set();
                                restart.get(io);
                                deadline = timestamp::now() +
                                    timedelta::milliseconds(100);
                                phase = 1; }
                            /* Should be no more timeouts once we
                             * start making progress again. */
                            else assert(phase == 1); } } } );
            firsttimeout.get(io);
            /* Should have hit the limit somewhere in there. */
            assert(clients.length() < 30);
            /* But not too soon. */
            assert(clients.length() > 5);
            /* Bump the limit back up again -> should start succeeding
             * again. */
            if (::setrlimit(RLIMIT_NOFILE, &oldlimit) < 0) {
                error::from_errno().fatal("resetting rlimit NOFILE"); }
            restart.set();
            /* Should start making progress again quickly. */
            assert(timedelta::time([&] {progress.get(io); } ) <
                   timedelta::seconds(2));
            /* Should finish fairly quickly as well. */
            assert(timedelta::time([&] { connector.get(); }) <
                   timedelta::milliseconds(200));
            /* Done. */
            s->destroy(io);
            while (!clients.empty()) delete clients.pophead(); });
    testcaseIO("rpc", "backpressure", [] (clientio io) {
            const timedelta lagtime(timedelta::seconds(1));
            auto s(rpcservice::listen<slowserver>(
                       peername::loopback(peername::port::any),
                       lagtime,
                       lagtime)
                   .fatal("starting slow server"));
            auto c(rpcclient::connect(io, s->localname())
                   .fatal("connecting to slow server"));
            auto starttime(timestamp::now());
            /* Fill the queue */
            list<rpcclient::asynccall *> calls;
            /* +2 is jsut to give ourselves a little bit of slack. */
            for (unsigned x = 0;
                 x < rpcserviceconfig::dflt.maxoutstanding + 2;
                 x++) {
                calls.pushtail(
                    c->call(wireproto::req_message(wireproto::msgtag(99)))); }
            /* Make sure server hasn't started processing calls yet. */
            assert(timestamp::now() - starttime < lagtime / 2);
            /* Next call shouldn't go through quickly. */
            assert(c->call(io,
                           wireproto::req_message(wireproto::msgtag(99)),
                           starttime + lagtime - timedelta::milliseconds(50))
                   == error::timeout);
            /* All calls should go through eventually. */
            while (!calls.empty()) {
                auto cl(calls.pophead());
                maybe<rpcclient::asynccall::token> t(Nothing);
                {   subscriber sub;
                    subscription ss(sub, cl->pub());
                    t = cl->finished();
                    while (t == Nothing) {
                        sub.wait(io);
                        t = cl->finished(); } }
                delete cl->pop(t.just()).fatal("late call"); }
            delete c;
            s->destroy(io); });
}
}
