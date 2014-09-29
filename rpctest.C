#include "rpctest.H"

#include <sys/socket.h>

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
            initlogging("T");
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
                if (mode == 0) s->destroy(io); }
            deinitlogging(); } );
}
}
