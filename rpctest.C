#include "rpctest.H"

#include "rpcconn.H"
#include "rpcserver.H"
#include "spark.H"
#include "test.H"
#include "thread.H"

#include "parsers.tmpl"
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
    return rpcconn::fromsocketnoauth<rpcconn>(
        s, slavename("<test client>"), actortype::test, rpcconnconfig::dflt);}};

class invrpcserver;

class invrpcconn : public rpcconn {
    friend class pausedthread<invrpcconn>;
public: invrpcserver &owner;
public: invrpcconn(const rpcconntoken &token,
                   invrpcserver &_owner)
    : rpcconn(token),
      owner(_owner) {}
public: void endconn(clientio); };

class invrpcserver : public rpcserver {
    friend class pausedthread<invrpcserver>;
    friend class thread;
public: rpcconn *connected;
public: publisher connectedpub;
public: publisher disconnectedpub;
public: invrpcserver(constoken t, listenfd fd)
    : rpcserver(t, fd),
      connected(NULL),
      connectedpub(),
      disconnectedpub() {}
public: orerror<rpcconn *> accept(socket_t s) {
    assert(connected == NULL);
    auto res(rpcconn::fromsocketnoauth<invrpcconn>(
                 s,
                 slavename("<test client>"),
                 actortype::test,
                 rpcconnconfig::dflt,
                 *this));
    if (res.isfailure()) return res.failure();
    connected = res.success();
    assert(connected->type() == actortype::test);
    connectedpub.publish();
    return res.success(); } };

void
invrpcconn::endconn(clientio) {
    assert(owner.connected != NULL);
    owner.connected = NULL;
    owner.disconnectedpub.publish(); }

static const wireproto::msgtag callabletag(134);
static const wireproto::msgtag bigreplytag(136);

class callablerpcconn : public rpcconn {
    friend class pausedthread<callablerpcconn>;
public: callablerpcconn(const rpcconntoken &token)
    : rpcconn(token) {}
public: messageresult message(
    const wireproto::rx_message &rxm,
    messagetoken token) {
    if (rxm.tag() == callabletag) {
        return new wireproto::resp_message(rxm); }
    else if (rxm.tag() == bigreplytag) {
        auto res(new wireproto::resp_message(rxm));
        char *buf = (char *)malloc(1024);
        memset(buf, 'X', 1024);
        buf[1023] = 0;
        string s(buf);
        for (uint16_t i = 1; i < 50 + random() % 50; i++) {
            res->addparam(wireproto::parameter<string>(i), s); }
        free(buf);
        return res; }
    else {
        return rpcconn::message(rxm, token); } } };

class callablerpcserver : public rpcserver {
    friend class pausedthread<callablerpcserver>;
    friend class thread;
private: const rpcconnconfig config;
public:  callablerpcserver(constoken t,
                           listenfd fd,
                           const rpcconnconfig &_config = rpcconnconfig::dflt)
    : rpcserver(t, fd),
      config(_config) {}
public:  orerror<rpcconn *> accept(socket_t s) {
    return rpcconn::fromsocketnoauth<callablerpcconn>(
        s, slavename("<test client>"), actortype::test, config); } };

class smallqueueserver : public rpcserver {
    friend class pausedthread<smallqueueserver>;
    friend class thread;
public: smallqueueserver(constoken t, listenfd fd)
    : rpcserver(t, fd) {}
public: orerror<rpcconn *> accept(socket_t s) {
    rpcconnconfig config(
        /* Small outgoing queue */
        100,
        /* Very slow ping machine. */
        timedelta::seconds(3600),
        timedelta::seconds(3600),
        /* Generous ping limiter */
        ratelimiterconfig(frequency::hz(1000000000),
                          1000000000));
    return rpcconn::fromsocketnoauth<rpcconn>(
        s, slavename("<test client>"), actortype::test, config); }
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
                rpcconnauth::mkwaithello(
                    ms,
                    rs,
                    [this] (orerror<rpcconn *> conn,
                            mutex_t::token /*authlock*/) {
                        connected = conn.dflt(NULL);
                        return Success; }),
                rpcconnconfig::dflt));
    if (ss.isfailure()) return ss;
    assert(ss.success()->slavename() == Nothing);
    assert(ss.success()->type() == Nothing);
    return ss.success(); } };

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
        rpcconnauth::mksendhelloslavea(
            rs,
            slavename("<test server>"),
            actortype::test),
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
    auto res(rpcconn::fromsocketnoauth<rpcconn>(
                 s,
                 slavename("<test client>"),
                 actortype::test,
                 rpcconnconfig::dflt));
    if (res.isfailure()) return res.failure();
    res.success()->send(clientio::CLIENTIO,
                        wireproto::tx_message(
                            sendbadhello
                            ? proto::HELLOSLAVE::A::tag
                            : wireproto::msgtag(73)))
        .fatal("sendinging message");
    return res; } };

class slowrpcconn : public rpcconn {
    friend class pausedthread<slowrpcconn>;
private: const waitbox<void> &unpause;
public:  slowrpcconn(const rpcconntoken &token,
                     const waitbox<void> &_unpause)
    : rpcconn(token),
      unpause(_unpause) {}
private: messageresult message(
    const wireproto::rx_message &rxm,
    messagetoken token) {
    unpause.get(clientio::CLIENTIO);
    return rpcconn::message(rxm, token); } };

class slowrpcserver : public rpcserver {
    friend class pausedthread<slowrpcserver>;
    friend class thread;
private: const waitbox<void> &unpause;
public:  slowrpcserver(constoken t, listenfd fd, const waitbox<void> &_unpause)
    : rpcserver(t, fd),
      unpause(_unpause) {}
public:  orerror<rpcconn *> accept(socket_t s) {
    auto config(rpcconnconfig::dflt);
    config.pinglimit = ratelimiterconfig(frequency::hz(100000000),
                                         1000000000);
    return rpcconn::fromsocketnoauth<slowrpcconn>(
        s,
        slavename("<test client>"),
        actortype::test,
        config,
        unpause); } };

static const wireproto::msgtag postedcalltag(101);
class postedconn : public rpcconn {
    friend class pausedthread<postedconn>;
private: const int mode;
private: const waitbox<void> *const unpause;
private: spark<void> *async;
public:  explicit postedconn(const rpcconntoken &token,
			     int _mode,
			     const waitbox<void> *_unpause)
    : rpcconn(token),
      mode(_mode),
      unpause(_unpause),
      async(NULL) {
    assert(mode >= 0);
    assert(mode <= 3);
    assert((mode == 3) == (unpause != NULL)); }
public:  messageresult message(
    const wireproto::rx_message &msg,
    messagetoken token) {
    if (msg.tag() != postedcalltag) return rpcconn::message(msg, token);
    auto c(new postedcall(this, msg, token));
    switch (mode) {
    case 0:
	c->complete(clientio::CLIENTIO);
	break;
    case 1:
	c->addparam(wireproto::parameter<int>(73), 902);
	c->complete(clientio::CLIENTIO);
	break;
    case 2:
	c->addparam(wireproto::parameter<int>(73), 902);
	c->fail(clientio::CLIENTIO, error::notafile);
	break;
    case 3:
	assert(unpause != NULL);
	async = new spark<void>([c, this] {
		unpause->get(clientio::CLIENTIO);
		c->complete(clientio::CLIENTIO); });
	break;
    default:
	abort(); }
    return c; }
public: void endconn(clientio) final {
    if (async) {
	async->get();
	delete async; } } };
class postedrpcserver : public rpcserver {
    friend class pausedthread<postedrpcserver>;
    friend class thread;
private: const int mode;
private: const waitbox<void> *const unpause;
public:  postedrpcserver(constoken t,
			 listenfd fd,
			 int _mode,
			 const waitbox<void> *_unpause = NULL)
    : rpcserver(t, fd),
      mode(_mode),
      unpause(_unpause) {}
public:  orerror<rpcconn *> accept(socket_t s) {
    return rpcconn::fromsocketnoauth<postedconn>(
	s,
	slavename("<test client>"),
	actortype::test,
	rpcconnconfig::dflt,
	mode,
	unpause); } };

static const wireproto::msgtag randomslowtag(99);
class randomslowconn : public rpcconn {
    friend class pausedthread<randomslowconn>;
private: list<spark<void> > sparks;
public:  explicit randomslowconn(const rpcconntoken &token)
    : rpcconn(token),
      sparks() {}
public:  messageresult message(
        const wireproto::rx_message &msg,
        messagetoken token) {
        if (msg.tag() != randomslowtag) return rpcconn::message(msg, token);
        auto res(new postedcall(this, msg, token));
        sparks.append([res] {
                (timestamp::now() + timedelta::milliseconds(random() % 1000))
                    .sleep(clientio::CLIENTIO);
                res->complete(clientio::CLIENTIO); });
        return res; }
public:  void endconn(clientio) { sparks.flush(); } };
class randomslowserver : public rpcserver {
    friend class pausedthread<randomslowserver>;
    friend class thread;
public:  randomslowserver(constoken t, listenfd fd)
    : rpcserver(t, fd) {}
public:  orerror<rpcconn *> accept(socket_t s) {
    return rpcconn::fromsocketnoauth<randomslowconn>(
        s,
        slavename("<test client>"),
        actortype::test,
        rpcconnconfig::dflt); } };

void
tests::_rpc() {
    testcaseV("rpc", "connconfig", [] {
            wireproto::roundtrip<rpcconnconfig>();
            parsers::roundtrip(parsers::_rpcconnconfig()); });
    testcaseIO("rpc", "trivserver", [] (clientio io) {
            /* Can we create and start an RPC server, without any clients? */
            peername listenon(peername::loopback(peername::port(quickcheck())));
            int x(73);
            auto s(::rpcserver::listen<trivrpcserver>(listenon, x)
                   .fatal("creating trivial server"));
            auto ss(s.unwrap());
            assert(ss->localname() == listenon);
            auto sss(s.go());
            assert(ss == sss);
            assert(sss->localname() == listenon);
            sss->destroy(io); });
    testcaseIO("rpc", "trivclient", [] (clientio io) {
            /* Can clients connect to our trivial server? */
            peername listenon(peername::loopback(peername::port(quickcheck())));
            int x(73);
            auto s1(::rpcserver::listen<trivrpcserver>(listenon, x)
                    .fatal("creating trivial server on " +
                           fields::mk(listenon)));
            auto s2(s1.go());
            auto c(rpcconn::connectnoauth<rpcconn>(
                       io,
                       slavename("<test server>"),
                       actortype::test,
                       listenon,
                       rpcconnconfig::dflt)
                   .fatal("connecting to trivial server"));
            delete c->call(
                io,
                wireproto::req_message(proto::PING::tag,
                                       c->allocsequencenr()))
                .fatal("sending ping");
            assert(c->call(io,
                           wireproto::req_message(proto::REMOVESTREAM::tag,
                                                  c->allocsequencenr()))
                   == error::unimplemented);
            c->destroy(io);
            s2->destroy(io); });
    testcaseIO("rpc", "authclient", [] (clientio io) {
            /* Basic auth state machine. */
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
                       io,
                       beaconresult(
                           ms.nonce(slavepeer),
                           slavepeer,
                           listenon,
                           rs),
                       slavename("HELLO"),
                       actortype::test,
                       rpcconnconfig::dflt)
                   .fatal("connecting to master"));
            delete c->call(
                io,
                wireproto::req_message(proto::PING::tag,
                                       c->allocsequencenr()))
                .fatal("sending ping");
            assert(s2->connected);
            c->destroy(io);
            assert(s2->connected->slavename() == slavename("HELLO"));
            s2->destroy(io); });
    testcaseIO("rpc", "badauthclient", [] (clientio io) {
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
            /* Messages other than PING and HELLO without HELLO should fail */
            {   auto c(rpcconn::connectnoauth<rpcconn>(
                           io,
                           slavename("<test server>"),
                           actortype::test,
                           listenon,
                           rpcconnconfig::dflt)
                       .fatal("connecting to master"));
                assert(c->call(
                           io,
                           wireproto::req_message(wireproto::msgtag(99),
                                                  c->allocsequencenr()))
                       == error::disconnected);
                c->destroy(io); }
            auto ff(
                [listenon]
                (std::function <wireproto::req_message & (wireproto::req_message &)> setup,
                 clientio _io) {
                    auto c(rpcconn::connectnoauth<rpcconn>(
                               _io,
                               slavename("<test server>"),
                               actortype::test,
                               listenon,
                               rpcconnconfig::dflt)
                           .fatal("connecting to master"));
                    wireproto::req_message msg(
                        proto::HELLO::tag,
                        c->allocsequencenr());
                    assert(c->call(_io, setup(msg))
                           == error::disconnected);
                    c->destroy(_io); });
            /* Bad HELLOs should fail */
            /* Missing parameters */
            ff([] (wireproto::req_message &t) -> wireproto::req_message & {
                    return t; },
                io);
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
                        .addparam(proto::HELLO::req::version, 99u)
                        .addparam(proto::HELLO::req::type, actortype::test); },
               io);
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
                        .addparam(proto::HELLO::req::version, 1u)
                        .addparam(proto::HELLO::req::type, actortype::test); },
               io);
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
                        .addparam(proto::HELLO::req::version, 1u)
                        .addparam(proto::HELLO::req::type, actortype::test); },
               io);
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
                        .addparam(proto::HELLO::req::version, 1u)
                        .addparam(proto::HELLO::req::type, actortype::test); },
                io);
            s2->destroy(io); });
    testcaseV("rpc", "statusrt", [] {
            wireproto::roundtrip< ::rpcserver::status_t>();
            wireproto::roundtrip<rpcconn::status_t>(); });
#if TESTING
    testcaseIO("rpc", "acceptfailed", [] (clientio io) {
            bool triggered = false;
            tests::eventwaiter<orerror<socket_t> &> w(
                tests::rpcserver::accepted,
                [io, &triggered] (orerror<socket_t> &fd) {
                    assert(!triggered);
                    fd.success().close();
                    fd = error::unknown;
                    triggered = true; });
            peername listenon(peername::loopback(peername::port::any));
            auto s1(::rpcserver::listen<trivrpcserver>(listenon, 73)
                    .fatal("creating trivial server"));
            auto s2(s1.go());
            auto c(rpcconn::connectnoauth<rpcconn>(
                       io,
                       slavename("<test server>"),
                       actortype::test,
                       s2->localname(),
                       rpcconnconfig::dflt));
            assert(c.success()->call(
                       io,
                       wireproto::req_message(
                           proto::PING::tag,
                           c.success()->allocsequencenr())) ==
                   error::disconnected);
            c.success()->destroy(io);
            s2->destroy(io); });
#endif
    testcaseIO("rpc", "unconnectable", [] (clientio io) {
            peername listenon(peername::loopback(peername::port(quickcheck())));
            auto s1(::rpcserver::listen<unconnectableserver>(listenon)
                    .fatal("creating trivial server"));
            auto s2(s1.go());
            auto c(rpcconn::connectnoauth<rpcconn>(
                       io,
                       slavename("<test server>"),
                       actortype::test,
                       listenon,
                       rpcconnconfig::dflt)
                   .fatal("connecting to trivial server"));
            assert(c->call(
                       io,
                       wireproto::req_message(proto::PING::tag,
                                              c->allocsequencenr())) ==
                   error::disconnected);
            c->destroy(io);
            s2->destroy(io); });
#if TESTING
    testcaseIO("rpc", "serverstatus", [] (clientio io) {
            peername listenon(peername::loopback(peername::port(quickcheck())));
            auto s1(::rpcserver::listen<unconnectableserver>(listenon)
                    .fatal("creating trivial server"));
            auto s2(s1.go());
            auto status1(s2->status());
            assert(s2->status() == status1);
            assert(!strcmp(fields::mk(status1).c_str(),
                           fields::mk(s2->status()).c_str()));
            /* Make sure status changes when we have pending connections. */
            waitbox<void> startedaccept;
            waitbox<void> finishaccept;
            tests::eventwaiter<void> w(
                tests::rpcserver::accepting,
                [&startedaccept, &finishaccept, io]
                () {
                    startedaccept.set();
                    finishaccept.get(io); });
            spark<orerror<rpcconn *> > connector([listenon, io] {
                    return rpcconn::connectnoauth<rpcconn>(
                        io,
                        slavename("<test server>"),
                        actortype::test,
                        listenon,
                        rpcconnconfig::dflt); });
            startedaccept.get(io);
            assert(!(s2->status() == status1));
            assert(strcmp(fields::mk(status1).c_str(),
                          fields::mk(s2->status()).c_str()));
            finishaccept.set();
            connector.get().success()->destroy(io);
            /* And make sure it goes back afterwards. */
	    auto status2(s2->status());
	    if (!(status2 == status1)) {
		initlogging("T");
		::logmsg(loglevel::emergency,
			 fields::mk(status2) + " != " + fields::mk(status1));
		abort(); }
            s2->destroy(io); });
#endif
    testcaseIO("rpc", "slaveauth", [] (clientio io) {
            peername listenon(peername::loopback(peername::port(quickcheck())));
            registrationsecret rs((quickcheck()));
            auto s1(::rpcserver::listen<slaverpcserver>(listenon, rs)
                    .fatal("creating trivial server"));
            auto s2(s1.go());
            auto c(rpcconn::connectslave<rpcconn>(
                       io,
                       listenon,
                       rs,
                       slavename("<test client>"),
                       actortype::test,
                       rpcconnconfig::dflt)
                   .fatal("connecting with slave auth"));
            delete c->call(
                io,
                wireproto::req_message(proto::PING::tag,
                                       c->allocsequencenr()))
                .fatal("sending ping");
            c->destroy(io);
            s2->destroy(io); });
    testcaseIO("rpc", "slaveauthbad", [] (clientio io) {
            peername listenon(peername::loopback(peername::port(quickcheck())));
            registrationsecret rs((quickcheck()));
            auto s1(::rpcserver::listen<slaverpcserver>(listenon, rs)
                    .fatal("creating trivial server"));
            auto s2(s1.go());

            /* Anything other than HELLOSLAVE::B should fail the
             * authentication machine and prevent further connections. */
            auto c(rpcconn::connectnoauth<rpcconn>(
                       io,
                       slavename("<test server>"),
                       actortype::test,
                       listenon,
                       rpcconnconfig::dflt)
                   .fatal("connecting with slave auth"));
            c->send(io, wireproto::tx_message(wireproto::msgtag(99)))
                .fatal("sending tag 99");
            assert(c->call(
                       io,
                       wireproto::req_message(proto::PING::tag,
                                              c->allocsequencenr()))
                   == error::disconnected);
            c->destroy(io);

            /* HELLOSLAVE::B with no digest should fail the
             * connection. */
            c = rpcconn::connectnoauth<rpcconn>(
                io,
                slavename("<test server>"),
                actortype::test,
                listenon,
                rpcconnconfig::dflt)
                .fatal("connecting with slave auth");
            c->send(io, wireproto::tx_message(
                        wireproto::msgtag(proto::HELLOSLAVE::B::tag)))
                .fatal("sending HELLOSLAVE::B with no digest");
            assert(c->call(
                       io,
                       wireproto::req_message(proto::PING::tag,
                                              c->allocsequencenr()))
                   == error::disconnected);
            c->destroy(io);

            /* Similarly a bad digest. */
            c = rpcconn::connectnoauth<rpcconn>(
                io,
                slavename("<test server>"),
                actortype::test,
                listenon,
                rpcconnconfig::dflt)
                .fatal("connecting with slave auth");
            c->send(io,
                    wireproto::tx_message(
                        wireproto::msgtag(proto::HELLOSLAVE::B::tag))
                    .addparam(proto::HELLOSLAVE::B::digest,
                              ::digest(fields::mk("invalid")))
                    .addparam(proto::HELLOSLAVE::B::name,
                              ::slavename("<bad digest client>"))
                    .addparam(proto::HELLOSLAVE::B::type,
                              actortype::test))
                .fatal("sending HELLOSLAVE::B with no digest");
            assert(c->call(
                       io,
                       wireproto::req_message(proto::PING::tag,
                                              c->allocsequencenr()))
                   == error::disconnected);
            c->destroy(io);
            s2->destroy(io); });
#if TESTING
    testcaseIO("rpc", "slaveautherr", [] (clientio io) {
            peername listenon(peername::loopback(peername::port(quickcheck())));
            registrationsecret rs((quickcheck()));
            auto s1(::rpcserver::listen<slaverpcserver>(listenon, rs)
                    .fatal("creating trivial server"));
            auto s2(s1.go());

            /* Things other than SLAVEHELLO::C should cause an
             * error. */
            {   eventwaiter<wireproto::tx_message **> evt(
                    tests::__rpcconn::sendinghelloslavec,
                    [] (wireproto::tx_message **msg) {
                        delete *msg;
                        *msg = new wireproto::tx_message(
                            wireproto::msgtag(99)); } );
                assert(rpcconn::connectslave<rpcconn>(
                           io,
                           listenon,
                           rs,
                           slavename("<bad hello client>"),
                           actortype::test,
                           rpcconnconfig::dflt)
                       == error::unrecognisedmessage); }
            /* HELLOSLAVE::C with an error should cause connectslave
             * to return an error. */
            {   eventwaiter<wireproto::tx_message **> evt(
                    tests::__rpcconn::sendinghelloslavec,
                    [] (wireproto::tx_message **msg) {
                        delete *msg;
                        *msg = new wireproto::tx_message(
                            proto::HELLOSLAVE::C::tag);
                        ::logmsg(loglevel::info, fields::mk("send error HELLOSLAVE::C"));
                        (*msg)->addparam(wireproto::err_parameter,
                                         error::pastend); });
                assert(rpcconn::connectslave<rpcconn>(
                           io,
                           listenon,
                           rs,
                           slavename("<test error client>"),
                           actortype::test,
                           rpcconnconfig::dflt)
                       == error::pastend); }
            s2->destroy(io); });
#endif
    testcaseIO("rpc", "dodgyopen", [] (clientio _io) {
            auto worker([] (bool sendbadhello, clientio io) {
                    auto s1(::rpcserver::listen<sendonconnectserver>(
                                peername::loopback(peername::port::any),
				sendbadhello)
                            .fatal("creating sendconnectserver"));
                    auto s2(s1.go());
                    registrationsecret rs((quickcheck()));
                    assert(rpcconn::connectslave<rpcconn>(
                               io,
                               s2->localname(),
                               rs,
                               slavename("<test client>"),
                               actortype::test,
                               rpcconnconfig::dflt) ==
                           (sendbadhello
                            ? error::missingparameter
                            : error::unrecognisedmessage));
                    s2->destroy(io); });
            worker(true, _io);
            worker(false, _io); });
#if TESTING
    testcaseIO("rpc", "queuefull", [] (clientio _io) {
            /* Create a server with a small outgoing queue and a
               client with a large one.  Pause the server's thread.
               Have the client send a load of PING messages until its
               queue becomes full, testing the send-queue-full path.
               Pause the client, then unpause the server.  The server
               should hit the reply-queue-full path.  Shut down the
               server.  Unpause the client. */
            auto work([] (bool cleanshutdown, clientio io) {
                    peername listenon(peername::loopback(
                                          peername::port(quickcheck())));
                    waitbox<void> serverpaused;
                    waitbox<void> unpauseserver;
                    waitbox<void> unpauseclient;
                    int phase = 0;
                    rpcconn *c = NULL;
                    eventwaiter<rpcconn *> waiter(
                        tests::__rpcconn::threadawoken,
                        [&serverpaused, &unpauseserver, &unpauseclient,
                         &phase, &c, io]
                        (rpcconn *who)
                        {   if (who == c) {
                                /* We are the client. */
                                if (phase == 0) {
                                    /* Let it run normally. */ }
                                else {
                                    /* Pause it. */
                                    unpauseclient.get(io); } }
                            else {
                                /* We are the server. */
                                if (phase == 0) {
                                    serverpaused.set();
                                    unpauseserver.get(io); }
                                else {
                                    /* Running normally. */ } } });
                    auto s1(::rpcserver::listen<smallqueueserver>(listenon)
                            .fatal("creating small queue server"));
                    auto s2(s1.go());

                    /* Server running, connect client. */
                    rpcconnconfig clientconfig(
                        /* Large outgoing queue */
                        1 << 20,
                        /* Slow ping machine */
                        timedelta::seconds(3600),
                        timedelta::seconds(3600),
                        /* Strict ping limiter */
                        ratelimiterconfig(frequency::hz(0.5),
                                          1));
                    c = rpcconn::connectnoauth<rpcconn>(
                        io,
                        slavename("<small queue server>"),
                        actortype::test,
                        listenon,
                        clientconfig)
                        .fatal("connecting large queue client");

                    /* Wait for server to pause. */
                    serverpaused.get(io);

                    /* Send until the queue is full */
                    int cntr = 0;
                    while (1) {
                        auto r(c->send(
                                   io,
                                   wireproto::tx_message(proto::PING::tag),
                                   (timestamp::now() +
                                    timedelta::milliseconds(10))));
                        if (r == error::timeout) break;
                        r.fatal("sending ping from small-queue client");
                        cntr++; }
                    ::logmsg(loglevel::info,
                             "sent " + fields::mk(cntr) + " pings");
                    /* Pause client, unpause server. */
                    {   waitbox<void> replyrestart;
                        waitbox<void> replystopped;
                        eventwaiter<void> replystoppedwaiter(
                            tests::__rpcconn::replystopped,
                            [&replystopped, &replyrestart, io] {
                                if (!replystopped.ready()) replystopped.set();
                                replyrestart.get(io); });
                        phase = 1;
                        unpauseserver.set();
                        if (!cleanshutdown) c->sock.close();
                        /* Wait for it to fill its queue generating
                         * replies. */
                        replystopped.get(io);
                        /* Unpause the server thread. */
                        replyrestart.set();
                        /* Give it a moment to do something. */
                        (timestamp::now() +
                         timedelta::milliseconds(100)).sleep(io);
                        /* Start shutdown sequence on the server. */
                        s2->shutdown.set(true);
                        /* Tear down the server. */
                        s2->destroy(io); }
                    /* Unpause the client and shut it down. */
                    unpauseclient.set();
                    c->destroy(io); });
            work(true, _io);
            work(false, _io);
            /* Done */ });
#endif
    testcaseIO("rpc", "shutdownbusy", [] (clientio _io) {
            auto work([] (bool serverfirst, clientio io) {
                    auto config(rpcconnconfig::dflt);
                    /* Effectively turn off ping limits so that things
                       run a bit more quickly. */
                    config.pinglimit = ratelimiterconfig(frequency::hz(1000),
                                                         1000000000);
                    auto s1(::rpcserver::listen<callablerpcserver>(
				peername::loopback(peername::port::any))
                            .fatal("creating callable RPC server"));
                    auto s2(s1.go());

                    waitbox<void> shutdownclients;
                    int cntr = 0;
                    /* Kick off a bunch of threads to keep the server
                     * busy. */
                    class worker {
                    private: class workerthr : public thread {
                    private: const waitbox<void> &shutdown;
                    private: int &cntr;
                    private: int localcntr;
                    private: const peername connectto;
                    private: void run(clientio __io) {
                        auto c(rpcconn::connectnoauth<rpcconn>(
                                   __io,
                                   slavename("<test server>"),
                                   actortype::test,
                                   connectto,
                                   rpcconnconfig::dflt)
                               .fatal("connecting to callable server"));
                        subscriber sub;
                        subscription ss(sub, shutdown.pub);
                        while (!shutdown.ready()) {
                            auto res(c->call(__io,
                                             wireproto::req_message(
                                                 callabletag,
                                                 c->allocsequencenr()),
                                             sub));
                            if (res.isfailure() &&
                                res.failure() == error::disconnected) {
                                (timestamp::now() +timedelta::milliseconds(100))
                                    .sleep(__io); }
                            else if (res.isfailure()) {
                                res.failure().fatal(
                                    "calling callable server"); }
                            else if (res.isnotified()) {
                                assert(res.notified() == &ss); }
                            else {
                                assert(res.issuccess());
                                delete res.success();
                                cntr++;
                                localcntr++; } }
                        /* Every thread client must make some amount
                         * of progress. */
                        assert(localcntr > 10);
                        c->destroy(__io); }
                    public:  workerthr(const constoken &token,
                                       const waitbox<void> &_shutdown,
                                       int &_cntr,
                                       const peername &_connectto)
                        : thread(token),
                          shutdown(_shutdown),
                          cntr(_cntr),
                          localcntr(0),
                          connectto(_connectto) {} };
                    public:  workerthr *inner;
                    public:  worker()
                        : inner(NULL) {}
                    public: void go(const peername &p,
                                    int &_cntr,
                                    const waitbox<void> &_shutdownclients) {
                        auto t1(thread::spawn<workerthr>(
                                    fields::mk("load gen"),
                                    _shutdownclients,
                                    _cntr,
                                    p));
                        inner = t1.go(); } };
                    const unsigned nr_workers = 2;
                    worker workers[nr_workers];
                    for (unsigned i = 0; i < nr_workers; i++) {
                        workers[i].go(s2->localname(), cntr, shutdownclients); }
                    /* Give it a moment for things to get started. */
                    (timestamp::now() + timedelta::milliseconds(500)).sleep(io);
                    /* Make sure that we've made progress */
                    assert(cntr > 1000);
                    int cntrsnap;
                    if (serverfirst) {
                        /* Shut down the server. */
                        s2->destroy(io);
                        /* Don't really need acquire semantics here,
                           it just discourages the compiler from doing
                           anything stupid. */
                        cntrsnap = loadacquire(cntr);
                        /* Make sure clients don't crash quickly. */
                        (timestamp::now() + timedelta::milliseconds(500))
                            .sleep(io); }
                    /* Shut the clients down. */
                    shutdownclients.set();
                    for (unsigned i = 0; i < nr_workers; i++) {
                        workers[i].inner->join(io); }
                    if (serverfirst) {
                        /* shouldn't have completed too many calls
                           after server shutdown.  Might get a couple
                           because cntr isn't properly
                           synchronised. */
                        assert(cntr >= cntrsnap);
                        assert(cntr <= cntrsnap + (int)nr_workers + 1); }
                    else {
                        s2->destroy(io); }
                    ::logmsg(loglevel::info,
                             "managed " + fields::mk(cntr) + " round trips");
                    /* We're done. */ });
            work(true, _io);
            work(false, _io); });

#if TESTING
    testcaseIO("rpc", "ping", [] (clientio io) {
            peername listenon(peername::loopback(peername::port(quickcheck())));
            /* Make the timeouts a bit more aggressive so that the
               test completes quickly. */
            rpcconnconfig config(
                /* buffer size */
                16384,
                /* Max idle time */
                timedelta::milliseconds(100),
                /* Ping deadline */
                timedelta::milliseconds(100),
                /* Ping limiter.  Leave it high to keep things easy. */
                ratelimiterconfig(frequency::hz(1000),
                                  100000));
            auto s1(::rpcserver::listen<callablerpcserver>(listenon, config)
                    .fatal("creating callable RPC server"));
            auto s2(s1.go());
            auto c(rpcconn::connectnoauth<rpcconn>(
                       io,
                       slavename("<test ping server>"),
                       actortype::test,
                       listenon,
                       config)
                   .fatal("connect to ping server"));
            /* Make sure we can make calls when things are running
             * normally. */
            delete c->call(
                io,
                wireproto::req_message(callabletag, c->allocsequencenr()))
                .fatal("calling ping server");
            (timestamp::now() + config.pinginterval * 2).sleep(io);
            delete c->call(
                io,
                wireproto::req_message(callabletag, c->allocsequencenr()))
                .fatal("calling ping server after stall");
            /* Stop the server from making further progress */
            waitbox<void> releaseserver;
            {   eventwaiter<rpcconn *> waiter(
                    tests::__rpcconn::threadawoken,
                    [c, &releaseserver, io]
                    (rpcconn *who) {
                        if (c == who) return;
                        releaseserver.get(io); });
                /* Next call should by killed by the ping machine. */
                auto starttime(timestamp::now());
                assert(c->call(
                           io,
                           wireproto::req_message(callabletag,
                                                  c->allocsequencenr()))
                       == error::disconnected);
                auto endtime(timestamp::now());
                /* Should have stopped us fairly quickly. */
                assert((endtime - starttime) <
                       config.pinginterval +
                       config.pingdeadline +
                       timedelta::milliseconds(50));
                c->destroy(io);
                releaseserver.set(); }
            /* Server unpaused -> connect again and try it the other
             * way around. */
            c = rpcconn::connectnoauth<rpcconn>(
                io,
                slavename("<test ping server>"),
                actortype::test,
                listenon,
                config)
                .fatal("connect to ping server");
            delete c->call(
                io,
                wireproto::req_message(callabletag, c->allocsequencenr()))
                .fatal("calling ping server after reconnect");
            waitbox<void> releaseclient;
            {   eventwaiter<rpcconn *> waiter(
                    tests::__rpcconn::threadawoken,
                    [c, &releaseclient, io]
                    (rpcconn *who) {
                        if (c != who) return;
                        releaseclient.get(io); });
                /* Wait for the ping machine to do its thing. */
                (timestamp::now() +
                 config.pinginterval +
                 config.pinginterval +
                 timedelta::milliseconds(50))
                    .sleep(io);
                /* Server should have noticed client death, so if we
                   unpause it we should find that we've been
                   disconnected. */
                releaseclient.set();
                auto starttime(timestamp::now());
                assert(c->call(
                           io,
                           wireproto::req_message(callabletag,
                                                  c->allocsequencenr()))
                       == error::disconnected);
                auto endtime(timestamp::now());
                assert((endtime - starttime) < timedelta::milliseconds(50));
                c->destroy(io); }
            s2->destroy(io); });
#endif
    testcaseIO("rpc", "sendbad", [] (clientio io) {
            peername listenon(peername::loopback(peername::port(quickcheck())));
            auto s1(::rpcserver::listen<callablerpcserver>(listenon)
                    .fatal("creating server"));
            auto s2(s1.go());
            auto c(rpcconn::connectnoauth<rpcconn>(
                       io,
                       slavename("<test server>"),
                       actortype::test,
                       listenon,
                       rpcconnconfig::dflt)
                   .fatal("connect to ping server"));
            delete c->call(
                io,
                wireproto::req_message(callabletag,
                                       c->allocsequencenr()))
                .fatal("call callable");
            /* Send a nonsense packet. */
            {   ::buffer b;
                char buf[4096];
                memset(buf, 0, sizeof(buf));
                b.queue(buf, sizeof(buf));
                subscriber sub;
                b.send(io,
                       c->sock,
                       sub).fatal("sending nonsense"); }
            /* call on the same connection should fail. */
            assert(c->call(
                       io,
                       wireproto::req_message(callabletag,
                                              c->allocsequencenr()))
                   == error::disconnected);
            /* Disconnect and reconnect should fix it. */
            c->destroy(io);
            c = rpcconn::connectnoauth<rpcconn>(
                       io,
                       slavename("<test server>"),
                       actortype::test,
                       listenon,
                       rpcconnconfig::dflt)
                .fatal("reconnect to ping server");
            c->call(io,
                    wireproto::req_message(callabletag,
                                           c->allocsequencenr()))
                .fatal("call after reconnect");
            c->destroy(io);
            s2->destroy(io); });

#if TESTING
    testcaseIO("rpc", "sendinterrupted", [] (clientio io) {
            peername listenon(peername::loopback(peername::port(quickcheck())));
            auto config(rpcconnconfig::dflt);
            config.pinglimit = ratelimiterconfig(frequency::hz(100000),
                                                 1000000);
            auto s1(::rpcserver::listen<callablerpcserver>(listenon, config)
                    .fatal("creating nullary server"));
            auto s2(s1.go());
            auto c(rpcconn::connectnoauth<rpcconn>(
                       io,
                       slavename("<test server>"),
                       actortype::test,
                       listenon,
                       rpcconnconfig::dflt)
                   .fatal("connect to nullary server"));
            waitbox<void> unpause;
            eventwaiter<rpcconn *> waiter(
                tests::__rpcconn::threadawoken,
                [c, &unpause, io]
                (rpcconn *who) {
                    if (who != c) unpause.get(io); });
            /* Fill the client's outgoing queue. */
            char bigbuf[10000];
            memset(bigbuf, 'X', 9999);
            bigbuf[9999] = '\0';
            string bigstr(bigbuf);
            spark<void> *sparks[100];
            for (uint16_t i = 0; i < 100; i++) {
                sparks[i] = new spark<void>(
                    [&bigstr, c, i, io] {
                        auto r(c->call(
                                   io,
                                   wireproto::req_message(
                                       proto::PING::tag,
                                       c->allocsequencenr())
                                   .addparam(
                                       wireproto::parameter<string>(
                                           (uint16_t)(i+1)),
                                       bigstr)));
                        if (r.issuccess()) {
                            delete r.success(); }
                        else {
                            assert(r.failure() == error::disconnected); } }); }
            /* Interrupt a send with a publish. */
            publisher pub;
            spark<void> publisher([&pub, io] () {
                    /* Let it get into place */
                    (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
                    pub.publish(); });
            subscriber sub;
            subscription ss(sub, pub);
            auto r(c->call(io,
                           wireproto::req_message(proto::PING::tag,
                                                  c->allocsequencenr()),
                           sub));
            assert(r.isnotified());
            assert(r.notified() == &ss);
            publisher.get();
            unpause.set();
            for (int i = 0; i < 100; i++) delete sparks[i];
            c->destroy(io);
            s2->destroy(io); });
#endif
    testcaseIO("rpc", "connectbadly", [] (clientio io) {
            /* Try to spoof a HELLO with a bad master secret. */
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
            assert(rpcconn::connectmaster<rpcconn>(
                       io,
                       beaconresult(
                           mastersecret::mk().nonce(slavepeer),
                           slavepeer,
                           listenon,
                           rs),
                       slavename("HELLO"),
                       actortype::test,
                       rpcconnconfig::dflt)
                   == error::disconnected);
            s2->destroy(io); });
    testcaseIO("rpc", "sendfailed", [] (clientio io) {
            peername listenon(peername::loopback(
                                  peername::port(quickcheck())));
            auto s1(::rpcserver::listen<callablerpcserver>(listenon)
                    .fatal("creating callable RPC server"));
            auto s2(s1.go());
            auto c(rpcconn::connectnoauth<rpcconn>(
                       io,
                       slavename("<test server>"),
                       actortype::test,
                       listenon,
                       rpcconnconfig::dflt)
                   .fatal("connecting to callable server"));
            /* Get the client RPC thread into a known place. */
            (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
            c->sock.close();
            assert(c->call(io,
                           wireproto::req_message(callabletag,
                                                  c->allocsequencenr()))
                   == error::disconnected);
            c->destroy(io);
            s2->destroy(io); });
    testcaseIO("rpc", "bigreply", [] (clientio io) {
            peername listenon(peername::loopback(
                                  peername::port(quickcheck())));
            auto s1(::rpcserver::listen<callablerpcserver>(listenon)
                    .fatal("creating callable RPC server"));
            auto s2(s1.go());
            auto c(rpcconn::connectnoauth<rpcconn>(
                       io,
                       slavename("<test server>"),
                       actortype::test,
                       listenon,
                       rpcconnconfig::dflt)
                   .fatal("connecting to callable server"));
            for (int i = 0; i < 10000; i++) {
                c->send(io,
                        wireproto::tx_message(bigreplytag))
                    .fatal("call big reply method"); }
            c->destroy(io);
            s2->destroy(io); });
    testcaseIO("rpc", "authtype", [] (clientio io) {
            peername listenon(peername::loopback(
                                  peername::port(quickcheck())));
            auto ms(mastersecret::mk());
            registrationsecret rs((quickcheck()));
            auto s1(::rpcserver::listen<authrpcserver>(listenon,
                                                       ms,
                                                       rs,
                                                       false)
                    .fatal("creating RPC server"));
            auto s2(s1.go());
            auto slavepeer(peername::loopback(peername::port(quickcheck())));
            auto c(rpcconn::connectmaster<rpcconn>(
                       io,
                       beaconresult(
                           ms.nonce(slavepeer),
                           slavepeer,
                           listenon,
                           rs),
                       slavename("HELLO"),
                       actortype::cli,
                       rpcconnconfig::dflt)
                   .fatal("connecting to master"));
            assert(c->type() == actortype::master);
            assert(c->localname().samehost(slavepeer));
            assert(s2->connected->type() == actortype::cli);
            c->destroy(io);
            s2->destroy(io); });


#if TESTING
    testcaseIO("rpc", "stalledrxcall", [] (clientio io) {
            auto s1(::rpcserver::listen<trivrpcserver>(
                        peername::loopback(peername::port::any),
                        73)
                    .fatal("listening"));
            auto s2(s1.go());
            auto f1(fields::mk(s2->status()).c_str());
            waitbox<void> restart;
            eventwaiter<void> waiter(
                tests::rpcserver::accepting,
                [&restart, io] {
                    restart.get(io); });
            auto c(rpcconn::connectnoauth<rpcconn>(
                       io,
                       slavename("<test server>"),
                       actortype::test,
                       s2->localname(),
                       rpcconnconfig::dflt)
                   .fatal("connecting to test server"));
            auto f2(fields::mk(s2->status()).c_str());
            assert(strcmp(f1, f2));
            f1 = fields::mk(c->status()).c_str();
            auto acall(c->callasync(
                           wireproto::req_message(
                               proto::PING::tag,
                               c->allocsequencenr())));
            assert(strcmp(fields::mk(c->status()).c_str(), f1));
            acall->destroy();
            c->destroy(io);
            restart.set();
            s2->destroy(io); });
#endif

    testcaseIO("rpc", "calltimedout", [] (clientio io) {
            auto s1(::rpcserver::listen<trivrpcserver>(
                        peername::loopback(peername::port::any),
                        73)
                    .fatal("listening"));
            auto s2(s1.go());
            auto c(rpcconn::connectnoauth<rpcconn>(
                       io,
                       slavename("<test server>"),
                       actortype::test,
                       s2->localname(),
                       rpcconnconfig::dflt)
                   .fatal("connecting to test server"));
            assert(c->call(
                       io,
                       wireproto::req_message(proto::PING::tag,
                                              c->allocsequencenr()),
                       timestamp::now())
                   == error::timeout);
            c->destroy(io);
            s2->destroy(io); });


    testcaseIO("rpc", "calldestroy", [] (clientio io) {
            waitbox<void> servergo;
            auto s1(::rpcserver::listen<slowrpcserver>(
                        peername::loopback(peername::port::any),
                        servergo)
                    .fatal("listening"));
            auto s2(s1.go());
            auto c(rpcconn::connectnoauth<rpcconn>(
                       io,
                       slavename("<test server>"),
                       actortype::test,
                       s2->localname(),
                       rpcconnconfig::dflt)
                   .fatal("connecting to test server"));
            /* Server is paused.  Stuff client's outgoing queue. */
            list<rpcconn::asynccall *> outstanding;
            for (int nr = 0; nr < 10000; nr++) {
                outstanding.pushtail(
                    c->callasync(wireproto::req_message(
                                     proto::PING::tag,
                                     c->allocsequencenr()))); }
            ::logmsg(loglevel::verbose, fields::mk("stuffed queue"));
            /* Tear them all down before unpausing the server.  Some
             * will have sent and be waiting for a reply, others will
             * still be waiting to send. */
            while (!outstanding.empty()) outstanding.poptail()->destroy();
            ::logmsg(loglevel::verbose, fields::mk("cancelled sends"));
            /* Unpause server. */
            servergo.set();
            /* Let one go all the way through before we cancel it. */
            auto call(c->callasync(wireproto::req_message(
                                       proto::PING::tag,
                                       c->allocsequencenr())));
            (timestamp::now() + timedelta::seconds(2)).sleep(io);
            ::logmsg(loglevel::verbose, fields::mk("cancel complete call"));
            call->destroy();
            /* And now do a normal call, to make sure everything's
             * still working. */
            call = c->callasync(wireproto::req_message(
                                    proto::PING::tag,
                                    c->allocsequencenr()));
            subscriber sub;
            subscription ss(sub, call->pub);
            while (true) {
                auto rr(call->popresult());
                if (rr == Nothing) {
                    sub.wait(io);
                    continue; }
                assert(rr.just().issuccess());
                delete rr.just().success();
                break; }
            c->destroy(io);
            s2->destroy(io); });

#if TESTING
    testcaseIO("rpc", "callrace", [] (clientio io) {
            /* Try to get the conn thread to do the send while we're
               doing a destroy. */
            auto s1(::rpcserver::listen<trivrpcserver>(
                        peername::loopback(peername::port::any),
                        73)
                    .fatal("listening"));
            auto s2(s1.go());
            auto c(rpcconn::connectnoauth<rpcconn>(
                       io,
                       slavename("<test server>"),
                       actortype::test,
                       s2->localname(),
                       rpcconnconfig::dflt)
                   .fatal("connecting to test server"));
            waitbox<void> waitbox1;
            waitbox<void> waitbox2;
            eventwaiter<rpcconn *> waiter1(
                tests::__rpcconn::calldestroyrace1,
                [&waitbox1, &waitbox2, c]
                (rpcconn *_c) {
                    if (c != _c) return;
                    waitbox1.set();
                    waitbox2.get(clientio::CLIENTIO); });
            eventwaiter<rpcconn *> waiter2(
                tests::__rpcconn::calldestroyrace2,
                [&waitbox1, c]
                (rpcconn *_c)
                {   if (c == _c) waitbox1.get(clientio::CLIENTIO); });
            eventwaiter<rpcconn *> waiter3(
                tests::__rpcconn::calldestroyrace3,
                [&waitbox2, c]
                (rpcconn *_c)
                {   if (c == _c) waitbox2.set(); });
            auto call(c->callasync(
                          wireproto::req_message(proto::PING::tag,
                                                 c->allocsequencenr())));
            (timestamp::now() + timedelta::milliseconds(200)).sleep(io);
            call->destroy();
            c->destroy(io);
            s2->destroy(io); });
#endif

    testcaseIO("rpc", "refcounts", [] (clientio io) {
            auto s1(::rpcserver::listen<invrpcserver>(
                        peername::loopback(peername::port::any))
                    .fatal("listening"));
            auto s2(s1.go());
            auto c(::rpcconn::connectnoauth<callablerpcconn>(
                       io,
                       slavename("<test server>"),
                       actortype::test,
                       s2->localname(),
                       rpcconnconfig::dflt)
                   .fatal("connecting"));
            {   subscriber sub;
                subscription ss(sub, s2->connectedpub);
                while (s2->connected == NULL) sub.wait(io); }
            auto c2(s2->connected);
            const int nr = 100;
            list<rpcconn::reftoken> tokens;
            for (int i = 0; i < nr; i++) tokens.pushtail(c2->reference());
            /* Disconnect the client.  Server side should remain
               viable until the reference drops. */
            c->destroy(io);
            {   subscriber sub;
                subscription ss(sub, s2->disconnectedpub);
                while (s2->connected != NULL) sub.wait(io); }
            /* give endconn() a chance to return */
            (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
            /* Check that call() returns an error without crashing. */
            assert(c2->call(io,
                            wireproto::req_message(callabletag,
                                                   c2->allocsequencenr()))
                   == error::disconnected);
            /* And that it still fails once we're down to our last
             * reference. */
            for (int i = 0; i < nr - 1; i++) c2->unreference(tokens.pophead());
            (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
            assert(c2->call(io,
                            wireproto::req_message(callabletag,
                                                   c2->allocsequencenr()))
                   == error::disconnected);
            /* Can't shut down server until reference drops. */
            bool dropped = false;
            spark<void> shutdowner([&dropped, s2, io] {
                    s2->destroy(io);
                    assert(dropped == true); });
            (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
            dropped = true;
            c2->unreference(tokens.pophead());
            /* We're done */ });

#if TESTING
    testcaseIO("rpc", "queuereplyshutdown", [] (clientio io) {
            /* Shut down a connection which is stalled waiting for TX
               space for an RPC reply */
            /* Create bigreply server */
            /* Server has a small outgoing queue */
            rpcconnconfig serverconfig(rpcconnconfig::dflt);
            serverconfig.maxoutgoingbytes = 1000;
            auto s1(::rpcserver::listen<callablerpcserver>(
                        peername::loopback(peername::port::any),
                        serverconfig)
                    .fatal("creating server"));
            auto s2(s1.go());
            /* Connect client */
            /* Client has a big outgoing queue */
            rpcconnconfig clientconfig(rpcconnconfig::dflt);
            clientconfig.maxoutgoingbytes = 100000000;
            auto c(rpcconn::connectnoauth<rpcconn>(
                       io,
                       slavename("<test server>"),
                       actortype::test,
                       s2->localname(),
                       clientconfig)
                   .fatal("connecting"));
            /* Pause the client rpcconn thread */
            waitbox<void> releaseclient;
            tests::eventwaiter<rpcconn *> waiter(
                tests::__rpcconn::threadawoken,
                [c, &releaseclient] (rpcconn *who) {
                    if (c == who) releaseclient.get(clientio::CLIENTIO); });
            /* Force the client to send a bunch of calls, even though
             * its thread is paused */
            list<rpcconn::asynccall *> calls;
            for (int i = 0; i < 1000; i++) {
                calls.pushtail(c->callasync(wireproto::req_message(
                                                bigreplytag,
                                                c->allocsequencenr()))); }
            c->txlock.locked([c] (mutex_t::token token) {
                    c->sendcalls(token); });
            c->txlock.locked([c, io] (mutex_t::token) {
                    while (!c->outgoing.empty()) {
                        subscriber sub;
                        /* Sync IO under a lock.  Okay because it's
                         * got a short timeout. */
                        auto r(c->outgoing.send(
                                   io,
                                   c->sock,
                                   sub,
                                   timestamp::now() + timedelta::milliseconds(100)));
                        /* If the server's filled its outgoing queue
                         * then it'll stop accepting incoming
                         * messages, and if we then fill our outgoing
                         * socket buf we'll deadlock.  Break it with a
                         * timeout. */
                        if (r == error::timeout) break;
                        assert(r == NULL); } });
            /* Abort all the calls. */
            while (!calls.empty()) calls.pophead()->destroy();
            /* Give the server a chance to get to where we want it. */
            (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
            /* It should now be stopped waiting for space to send more
             * replies.  Shut it down. */
            s2->destroy(io);
            /* We're done; shut everything else down. */
            releaseclient.set();
            c->destroy(io); });
#endif
    testcaseIO("rpc", "postedcallsbasic", [] (clientio io) {
            /* Basic posted calls must work. */
	    auto s1(::rpcserver::listen<postedrpcserver>(
			peername::loopback(peername::port::any),
			/*mode*/ 0)
		    .fatal("listen"));
	    auto s2(s1.go());
	    auto c(::rpcconn::connectnoauth<rpcconn>(
		       io,
                       slavename("<test server>"),
                       actortype::test,
                       s2->localname(),
                       rpcconnconfig::dflt)
                   .fatal("connecting"));
	    delete c->call(io,
			   wireproto::req_message(
			       postedcalltag,
			       c->allocsequencenr()))
		.fatal("basic call");
	    c->destroy(io);
	    s2->destroy(io); });
    testcaseIO("rpc", "postedcallsparams", [] (clientio io) {
	    /* posted call with a response parameter */
	    auto s1(::rpcserver::listen<postedrpcserver>(
			peername::loopback(peername::port::any),
			/*mode*/ 1)
		    .fatal("listen"));
	    auto s2(s1.go());
	    auto c(::rpcconn::connectnoauth<rpcconn>(
		       io,
                       slavename("<test server>"),
                       actortype::test,
                       s2->localname(),
                       rpcconnconfig::dflt)
                   .fatal("connecting"));
	    auto resp(c->call(io,
			      wireproto::req_message(
			       postedcalltag,
			       c->allocsequencenr()))
		      .fatal("basic call"));
	    assert(resp->getparam(wireproto::parameter<int>(73)) == 902);
	    delete resp;
	    c->destroy(io);
	    s2->destroy(io); });
    testcaseIO("rpc", "postedcallserr", [] (clientio io) {
	    /* posted call with a response error */
	    auto s1(::rpcserver::listen<postedrpcserver>(
			peername::loopback(peername::port::any),
			/*mode*/ 2)
		    .fatal("listen"));
	    auto s2(s1.go());
	    auto c(::rpcconn::connectnoauth<rpcconn>(
		       io,
                       slavename("<test server>"),
                       actortype::test,
                       s2->localname(),
                       rpcconnconfig::dflt)
                   .fatal("connecting"));
	    assert(c->call(io,
			   wireproto::req_message(
			       postedcalltag,
			       c->allocsequencenr()))
		   == error::notafile);
	    c->destroy(io);
	    s2->destroy(io); });
    testcaseIO("rpc", "postedcallsoverlap", [] (clientio io) {
            /* Must be able to make sync calls while there are posted
             * calls outstanding. */
	    waitbox<void> unpauseserver;
	    auto s1(::rpcserver::listen<postedrpcserver>(
			peername::loopback(peername::port::any),
			/*mode*/ 3,
			&unpauseserver)
		    .fatal("listen"));
	    auto s2(s1.go());
	    auto c(::rpcconn::connectnoauth<rpcconn>(
		       io,
                       slavename("<test server>"),
                       actortype::test,
                       s2->localname(),
                       rpcconnconfig::dflt)
                   .fatal("connecting"));
	    spark<void> call1([c, io] {
		    delete c->call(io,
				   wireproto::req_message(
				       postedcalltag, c->allocsequencenr()))
			.fatal("postedcall"); });
	    (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
	    delete c->call(io,
			   wireproto::req_message(
			       proto::PING::tag,
			       c->allocsequencenr()))
		.fatal("ping");
	    unpauseserver.set();
	    call1.get();
	    s2->destroy(io);
	    c->destroy(io); });
    testcaseIO("rpc", "randomslow", [] (clientio io) {
            auto s1(::rpcserver::listen<randomslowserver>(
                        peername::loopback(peername::port::any))
                    .fatal("listening"));
            auto s2(s1.go());
            auto c(::rpcconn::connectnoauth<rpcconn>(
                       io,
                       slavename("<test server>"),
                       actortype::test,
                       s2->localname(),
                       rpcconnconfig::dflt)
                   .fatal("connecting"));
            /* Launch a bunch of async calls against the randomly slow
             * server, from a bunch of different threads. */
            const int nrthreads = 16;
            const int callsperthread = 1024;
            rpcconn::asynccall *calls[nrthreads][callsperthread];
            list<spark<void> > threads;
            for (int i = 0; i < nrthreads; i++) {
                threads.append([c, &calls, i] {
                        for (int j = 0; j < callsperthread; j++) {
                            calls[i][j] = c->callasync(
                                wireproto::req_message(
                                    randomslowtag,
                                    c->allocsequencenr())); } }); }
            threads.flush();
            /* Wait for the calls to finish, again from a bunch of
             * different threads. */
            for (int i = 0; i < nrthreads; i++) {
                threads.append([c, calls, i, io] {
                        for (int j = 0; j < callsperthread; j++) {
                            auto r(calls[i][j]->popresult());
                            if (r == Nothing) {
                                subscriber sub;
                                subscription ss(sub, calls[i][j]->pub);
                                r = calls[i][j]->popresult();
                                while (r == Nothing) {
                                    sub.wait(io);
                                    r = calls[i][j]->popresult(); } }
                            delete r.just().success(); } }); }
            threads.flush();
            c->destroy(io);
            s2->destroy(io); }); }
