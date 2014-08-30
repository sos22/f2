#include "rpctest.H"

#include "rpcconn.H"
#include "rpcserver.H"
#include "spark.H"
#include "test.H"

#include "rpcconn.tmpl"
#include "rpcserver.tmpl"
#include "spark.tmpl"
#include "test.tmpl"
#include "wireproto.tmpl"

class trivrpcserver : public rpcserver {
    friend class pausedthread<trivrpcserver>;
    friend class thread;
public: trivrpcserver(constoken t, listenfd fd, int x)
    : rpcserver(t, fd) {
    assert(x == 73); }
public: orerror<rpcconn *> accept(socket_t s) {
    return rpcconn::fromsocket<rpcconn>(
        s, rpcconnauth::mkdone(rpcconnconfig::dflt), rpcconnconfig::dflt); }
};

class authrpcserver : public rpcserver {
    friend class pausedthread<authrpcserver>;
    friend class thread;
private: const mastersecret &ms;
private: const registrationsecret &rs;
public:  rpcconn *connected;
private: const bool allowmulti;
public:  authrpcserver(constoken t,
                       listenfd fd,
                       const mastersecret &_ms,
                       const registrationsecret &_rs,
                       bool _allowmulti)
    : rpcserver(t, fd),
      ms(_ms),
      rs(_rs),
      connected(NULL),
      allowmulti(_allowmulti) {}
public:  orerror<rpcconn *> accept(socket_t s) {
    assert(connected == NULL || allowmulti);
    auto ss(rpcconn::fromsocket<rpcconn>(
                s,
                rpcconnauth::mkwaithello(ms, rs, rpcconnconfig::dflt),
                rpcconnconfig::dflt));
    if (ss.isfailure()) return ss;
    connected = ss.success();
    assert(connected->slavename() == Nothing);
    return connected; } };

class unconnectableserver : public rpcserver {
    friend class pausedthread<unconnectableserver>;
    friend class thread;
public:  unconnectableserver(constoken t,
                             listenfd fd)
    : rpcserver(t, fd) {}
public:  orerror<rpcconn *> accept(socket_t) {
    return error::nothing; }
};

class slaverpcserver : public rpcserver {
    friend class pausedthread<slaverpcserver>;
    friend class thread;
private: const registrationsecret &rs;
public:  slaverpcserver(constoken t,
                        listenfd fd,
                        const registrationsecret &_rs)
    : rpcserver(t, fd),
      rs(_rs) {}
public:  orerror<rpcconn *> accept(socket_t s) {
    logmsg(loglevel::info, fields::mk("accepting slaverpc connection"));
    return rpcconn::fromsocket<rpcconn>(
        s,
        rpcconnauth::mksendhelloslavea(rs, rpcconnconfig::dflt),
        rpcconnconfig::dflt); }
};

class sendonconnectserver : public rpcserver {
    friend class pausedthread<sendonconnectserver>;
    friend class thread;
private: const bool sendbadhello;
public:  sendonconnectserver(constoken t, listenfd fd, bool _sendbadhello)
    : rpcserver(t, fd),
      sendbadhello(_sendbadhello) {}
public: orerror<rpcconn *> accept(socket_t s) {
    auto res(
        rpcconn::fromsocket<rpcconn>(
            s, rpcconnauth::mkdone(rpcconnconfig::dflt), rpcconnconfig::dflt));
    if (res.isfailure()) return res.failure();
    res.success()->send(clientio::CLIENTIO,
                        wireproto::tx_message(
                            sendbadhello
                            ? proto::HELLOSLAVE::A::tag
                            : wireproto::msgtag(73)))
        .fatal("sendinging message");
    return res; } };

void
tests::_rpc() {
    testcaseV("rpc", "trivserver", [] {
            /* Can we create and start an RPC server, without any clients? */
            initpubsub();
            peername listenon(peername::loopback(peername::port(quickcheck())));
            int x(73);
            auto s(::rpcserver::listen<trivrpcserver>(listenon, x)
                   .fatal("creating trivial server"));
            auto ss(s.unwrap());
            assert(ss->localname() == listenon);
            auto sss(s.go());
            assert(ss == sss);
            assert(sss->localname() == listenon);
            sss->destroy(clientio::CLIENTIO);
            deinitpubsub(clientio::CLIENTIO); });
    testcaseV("rpc", "trivclient", [] {
            /* Can clients connect to our trivial server? */
            initpubsub();
            peername listenon(peername::loopback(peername::port(quickcheck())));
            int x(73);
            auto s1(::rpcserver::listen<trivrpcserver>(listenon, x)
                    .fatal("creating trivial server on " +
                           fields::mk(listenon)));
            auto s2(s1.go());
            auto c(rpcconn::connect<rpcconn>(
                       clientio::CLIENTIO,
                       rpcconnauth::mkdone(rpcconnconfig::dflt),
                       listenon,
                       rpcconnconfig::dflt)
                   .fatal("connecting to trivial server"));
            delete c->call(
                clientio::CLIENTIO,
                wireproto::req_message(proto::PING::tag,
                                       c->allocsequencenr()))
                .fatal("sending ping");
            assert(c->call(clientio::CLIENTIO,
                           wireproto::req_message(proto::REMOVESTREAM::tag,
                                                  c->allocsequencenr()))
                   == error::unimplemented);
            c->destroy(clientio::CLIENTIO);
            s2->destroy(clientio::CLIENTIO);
            deinitpubsub(clientio::CLIENTIO); });
    testcaseV("rpc", "authclient", [] {
            /* Basic auth state machine. */
            initpubsub();
            peername listenon(peername::loopback(peername::port(quickcheck())));
            auto ms(mastersecret::mk());
            registrationsecret rs((quickcheck()));
            bool _false(false);
            auto s1(::rpcserver::listen<authrpcserver>(
                        listenon,
                        ms,
                        rs,
                        _false).
                    fatal("listen for auth"));
            auto s2(s1.go());
            auto slavepeer(peername::loopback(peername::port(quickcheck())));
            auto c(rpcconn::connectmaster<rpcconn>(
                       clientio::CLIENTIO,
                       beaconresult(
                           ms.nonce(slavepeer),
                           slavepeer,
                           listenon,
                           rs),
                       slavename("HELLO"),
                       rpcconnconfig::dflt)
                   .fatal("connecting to master"));
            delete c->call(
                clientio::CLIENTIO,
                wireproto::req_message(proto::PING::tag,
                                       c->allocsequencenr()))
                .fatal("sending ping");
            assert(s2->connected);
            c->destroy(clientio::CLIENTIO);
            assert(s2->connected->slavename() == slavename("HELLO"));
            s2->destroy(clientio::CLIENTIO);
            deinitpubsub(clientio::CLIENTIO); });
    testcaseV("rpc", "badauthclient", [] {
            initlogging("T");
            initpubsub();
            peername listenon(peername::loopback(peername::port(quickcheck())));
            auto ms(mastersecret::mk());
            registrationsecret rs((quickcheck()));
            bool _true(true);
            auto s1(::rpcserver::listen<authrpcserver>(
                        listenon,
                        ms,
                        rs,
                        _true).
                    fatal("listen for auth"));
            auto s2(s1.go());
            auto slavepeer(peername::loopback(peername::port(quickcheck())));
            /* PING without HELLO should fail */
            {   auto c(rpcconn::connect<rpcconn>(
                           clientio::CLIENTIO,
                           rpcconnauth::mkdone(rpcconnconfig::dflt),
                           listenon,
                           rpcconnconfig::dflt)
                       .fatal("connecting to master"));
                assert(c->call(
                           clientio::CLIENTIO,
                           wireproto::req_message(wireproto::msgtag(99),
                                                  c->allocsequencenr()))
                       == error::disconnected);
                c->destroy(clientio::CLIENTIO); }
            auto ff(
                [listenon]
                (std::function <wireproto::req_message & (wireproto::req_message &)> setup) {
                    auto c(rpcconn::connect<rpcconn>(
                               clientio::CLIENTIO,
                               rpcconnauth::mkdone(rpcconnconfig::dflt),
                               listenon,
                               rpcconnconfig::dflt)
                           .fatal("connecting to master"));
                    wireproto::req_message msg(
                        proto::HELLO::tag,
                        c->allocsequencenr());
                    assert(c->call(clientio::CLIENTIO, setup(msg))
                           == error::disconnected);
                    c->destroy(clientio::CLIENTIO); });
            /* Bad HELLOs should fail */
            /* Missing parameters */
            ff([] (wireproto::req_message &t) -> wireproto::req_message & {
                    return t; });
            /* Bad version */
            ff([&ms, slavepeer]
               (wireproto::req_message &t) -> wireproto::req_message & {
                    return t.addparam(proto::HELLO::req::digest,
                                      digest(fields::mk("foo")))
                        .addparam(proto::HELLO::req::nonce,
                                  ms.nonce(slavepeer))
                        .addparam(proto::HELLO::req::peername,
                                  slavepeer)
                        .addparam(proto::HELLO::req::slavename,
                                  slavename("HELLO"))
                        .addparam(proto::HELLO::req::version, 99u); });
            /* Bad nonce */
            ff([slavepeer]
               (wireproto::req_message &t) -> wireproto::req_message & {
                    return t.addparam(proto::HELLO::req::digest,
                                      digest(fields::mk("foo")))
                        .addparam(proto::HELLO::req::nonce,
                                  mastersecret::mk().nonce(slavepeer))
                        .addparam(proto::HELLO::req::peername,
                                  slavepeer)
                        .addparam(proto::HELLO::req::slavename,
                                  slavename("HELLO"))
                        .addparam(proto::HELLO::req::version, 1u); });
            /* Bad peername */
            ff([&ms]
               (wireproto::req_message &t) -> wireproto::req_message & {
                    peername slavepeer2((quickcheck()));
                    return t.addparam(proto::HELLO::req::digest,
                                      digest(fields::mk("foo")))
                        .addparam(proto::HELLO::req::nonce,
                                  ms.nonce(slavepeer2))
                        .addparam(proto::HELLO::req::peername,
                                  slavepeer2)
                        .addparam(proto::HELLO::req::slavename,
                                  slavename("HELLO"))
                        .addparam(proto::HELLO::req::version, 1u); });
            /* Bad digest */
            ff([&ms, slavepeer]
               (wireproto::req_message &t) -> wireproto::req_message & {
                    return t.addparam(proto::HELLO::req::digest,
                                      digest(fields::mk("foo")))
                        .addparam(proto::HELLO::req::nonce,
                                  ms.nonce(slavepeer))
                        .addparam(proto::HELLO::req::peername,
                                  slavepeer)
                        .addparam(proto::HELLO::req::slavename,
                                  slavename("HELLO"))
                        .addparam(proto::HELLO::req::version, 1u); });
            s2->destroy(clientio::CLIENTIO);
            deinitpubsub(clientio::CLIENTIO);
            deinitlogging();});
    testcaseV("rpc", "statusrt", [] {
            wireproto::roundtrip< ::rpcserver::status_t>();
            wireproto::roundtrip<rpcconn::status_t>(); });
#if TESTING
    testcaseV("rpc", "acceptfailed", [] {
            initlogging("T");
            initpubsub();
            bool triggered = false;
            publisher pub;
            tests::eventwaiter<listenfd> w(
                tests::rpcserver::accepting,
                [&triggered, &pub] (listenfd fd) {
                    assert(!triggered);
                    fd.close();
                    triggered = true;
                    pub.publish(); });
            peername listenon(peername::loopback(peername::port(quickcheck())));
            int x(73);
            auto s1(::rpcserver::listen<trivrpcserver>(listenon, x)
                    .fatal("creating trivial server"));
            auto s2(s1.go());
            subscriber sub;
            subscription ss(sub, pub);
            auto c(rpcconn::connect<rpcconn>(
                       clientio::CLIENTIO,
                       rpcconnauth::mkdone(rpcconnconfig::dflt),
                       listenon,
                       rpcconnconfig::dflt)
                   .fatal("connecting to trivial server"));
            while (!triggered) sub.wait(clientio::CLIENTIO);
            assert(c->call(
                       clientio::CLIENTIO,
                       wireproto::req_message(proto::PING::tag,
                                              c->allocsequencenr())) ==
                   error::disconnected);
            c->destroy(clientio::CLIENTIO);
            s2->destroy(clientio::CLIENTIO);
            deinitpubsub(clientio::CLIENTIO);
            deinitlogging(); });
#endif
    testcaseV("rpc", "unconnectable", [] {
            initpubsub();
            peername listenon(peername::loopback(peername::port(quickcheck())));
            auto s1(::rpcserver::listen<unconnectableserver>(listenon)
                    .fatal("creating trivial server"));
            auto s2(s1.go());
            auto c(rpcconn::connect<rpcconn>(
                       clientio::CLIENTIO,
                       rpcconnauth::mkdone(rpcconnconfig::dflt),
                       listenon,
                       rpcconnconfig::dflt)
                   .fatal("connecting to trivial server"));
            assert(c->call(
                       clientio::CLIENTIO,
                       wireproto::req_message(proto::PING::tag,
                                              c->allocsequencenr())) ==
                   error::disconnected);
            c->destroy(clientio::CLIENTIO);
            s2->destroy(clientio::CLIENTIO);
            deinitpubsub(clientio::CLIENTIO); });
#if TESTING
    testcaseV("rpc", "serverstatus", [] {
            initpubsub();
            peername listenon(peername::loopback(peername::port(quickcheck())));
            auto s1(::rpcserver::listen<unconnectableserver>(listenon)
                    .fatal("creating trivial server"));
            auto s2(s1.go());
            auto status1(s2->status());
            assert(s2->status() == status1);
            assert(!strcmp(fields::mk(status1).c_str(),
                           fields::mk(s2->status()).c_str()));
            /* Make sure status changes when we have pending connections. */
            waitbox<bool> startedaccept;
            waitbox<bool> finishaccept;
            tests::eventwaiter<listenfd> w(
                tests::rpcserver::accepting,
                [&startedaccept, &finishaccept]
                (listenfd) {
                    startedaccept.set(true);
                    assert(finishaccept.get(clientio::CLIENTIO) == true); });
            spark<orerror<rpcconn *> > connector([listenon] {
                    return rpcconn::connect<rpcconn>(
                        clientio::CLIENTIO,
                        rpcconnauth::mkdone(rpcconnconfig::dflt),
                        listenon,
                        rpcconnconfig::dflt); });
            assert(startedaccept.get(clientio::CLIENTIO) == true);
            assert(!(s2->status() == status1));
            assert(strcmp(fields::mk(status1).c_str(),
                          fields::mk(s2->status()).c_str()));
            finishaccept.set(true);
            connector.get().success()->destroy(clientio::CLIENTIO);
            /* And make sure it goes back afterwards. */
            assert(s2->status() == status1);
            s2->destroy(clientio::CLIENTIO);
            deinitpubsub(clientio::CLIENTIO); });
#endif
    testcaseV("rpc", "slaveauth", [] {
            initlogging("T");
            initpubsub();
            peername listenon(peername::loopback(peername::port(quickcheck())));
            registrationsecret rs((quickcheck()));
            auto s1(::rpcserver::listen<slaverpcserver>(listenon, rs)
                    .fatal("creating trivial server"));
            auto s2(s1.go());
            auto c(rpcconn::connectslave<rpcconn>(
                       clientio::CLIENTIO,
                       listenon,
                       rs,
                       rpcconnconfig::dflt)
                   .fatal("connecting with slave auth"));
            delete c->call(
                clientio::CLIENTIO,
                wireproto::req_message(proto::PING::tag,
                                       c->allocsequencenr()))
                .fatal("sending ping");
            c->destroy(clientio::CLIENTIO);
            s2->destroy(clientio::CLIENTIO);
            deinitpubsub(clientio::CLIENTIO);
            deinitlogging(); });
#if TESTING
    testcaseV("rpc", "pendingreply", [] {
            initpubsub();
            peername listenon(peername::loopback(peername::port(quickcheck())));
            registrationsecret rs((quickcheck()));
            auto s1(::rpcserver::listen<slaverpcserver>(listenon, rs)
                    .fatal("creating trivial server"));
            auto s2(s1.go());
            auto c(rpcconn::connectslave<rpcconn>(
                       clientio::CLIENTIO,
                       listenon,
                       rs,
                       rpcconnconfig::dflt)
                   .fatal("connecting with slave auth"));
            auto status1(c->status(Nothing));
            assert(!strcmp(fields::mk(status1).c_str(),
                           fields::mk(c->status(Nothing)).c_str()));
            waitbox<void> donerx;
            waitbox<void> readyrx;
            tests::eventwaiter<void> evt1(
                tests::__rpcconn::calldonetx,
                [&readyrx] { readyrx.get(clientio::CLIENTIO); });
            tests::eventwaiter<void> evt2(
                tests::__rpcconn::receivedreply,
                [&donerx] { donerx.set(); });
            spark<void> caller(
                [c] {
                    delete c->call(
                        clientio::CLIENTIO,
                        wireproto::req_message(proto::PING::tag,
                                               c->allocsequencenr()))
                        .fatal("sending ping"); });
            donerx.get(clientio::CLIENTIO);
            auto status2(c->status(Nothing));
            assert(strcmp(fields::mk(status1).c_str(),
                          fields::mk(status2).c_str()));
            readyrx.set();
            caller.get();
            auto status3(c->status(Nothing));
            assert(strcmp(fields::mk(status3).c_str(),
                          fields::mk(status1).c_str()));
            assert(strcmp(fields::mk(status3).c_str(),
                          fields::mk(status2).c_str()));
            c->destroy(clientio::CLIENTIO);
            s2->destroy(clientio::CLIENTIO);
            deinitpubsub(clientio::CLIENTIO); });
#endif
    testcaseV("rpc", "dodgyopen", [] {
            initpubsub();
            auto worker([] (bool sendbadhello) {
                    peername listenon(peername::loopback(
                                          peername::port(quickcheck())));
                    auto s1(::rpcserver::listen<sendonconnectserver>(
                                listenon, sendbadhello)
                            .fatal("creating sendconnectserver"));
                    auto s2(s1.go());
                    registrationsecret rs((quickcheck()));
                    assert(rpcconn::connectslave<rpcconn>(
                               clientio::CLIENTIO,
                               listenon,
                               rs,
                               rpcconnconfig::dflt) ==
                           (sendbadhello
                            ? error::missingparameter
                            : error::unrecognisedmessage));
                    s2->destroy(clientio::CLIENTIO); });
            worker(true);
            worker(false);
            deinitpubsub(clientio::CLIENTIO); });

}
