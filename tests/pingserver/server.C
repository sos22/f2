#include "logging.H"
#include "proto.H"
#include "rpcconn.H"
#include "rpcserver.H"
#include "shutdown.H"

#include "either.tmpl"
#include "list.tmpl"
#include "rpcconn.tmpl"
#include "rpcserver.tmpl"

class pingableconn : public rpcconn {
    friend class thread;
    friend class pausedthread<pingableconn>;
public:  waitbox<shutdowncode> &shutdown;
private: pingableconn(rpcconntoken token,
                      waitbox<shutdowncode> &_shutdown)
    : rpcconn(token),
      shutdown(_shutdown) {}
public:  messageresult message(const wireproto::rx_message &);
public:  ~pingableconn() {}
};

class pingableserver : public rpcserver {
    friend class pausedthread<pingableserver>;
    friend class thread;
public:  pingableserver(constoken t, listenfd fd)
    : rpcserver(t, fd) {}
public:  waitbox<shutdowncode> shutdown;
public:  static orerror<pingableserver *> listen(
    const peername &p);
private: orerror<rpcconn *> accept(socket_t sock);
private: ~pingableserver() {}
};

messageresult
pingableconn::message(const wireproto::rx_message &msg) {
    if (msg.tag() == proto::QUIT::tag) {
        auto code(msg.getparam(proto::QUIT::req::reason));
        if (code == Nothing) return error::missingparameter;
        shutdown.set(code.just());
        return messageresult::noreply;
    } else {
        return rpcconn::message(msg); } }

orerror<pingableserver *>
pingableserver::listen(const peername &p) {
    return rpcserver::listen<pingableserver>(p)
        .map<pingableserver *>([] (pausedrpcserver<pingableserver> s) {
                return s.go(); }); }

orerror<rpcconn *>
pingableserver::accept(socket_t s) {
    return rpcconn::fromsocket<pingableconn>(
        s,
        rpcconnauth::mkdone(slavename("<ping client>"),
                            actortype::test,
                            rpcconnconfig::dflt),
        rpcconnconfig::dflt,
        shutdown); }

int
main()
{
    initlogging("pingableserver");
    initpubsub();
    auto server(pingableserver::listen(peername::all(peername::port::any))
                .fatal("listening"));
    fields::print("listening on " + fields::mk(server->localname())
                  + "\n");
    auto r(server->shutdown.get(clientio::CLIENTIO));
    server->destroy(clientio::CLIENTIO);
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging();
    r.finish();
}
