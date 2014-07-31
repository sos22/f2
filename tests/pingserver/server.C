#include "logging.H"
#include "proto.H"
#include "rpcconn.H"
#include "rpcserver.H"
#include "shutdown.H"

#include "either.tmpl"
#include "list.tmpl"
#include "rpcconn.tmpl"

class pingableconn : public rpcconn {
    friend class rpcconn;
public:  waitbox<shutdowncode> &shutdown;
private: pingableconn(socket_t _sock,
                      const rpcconnauth &__auth,
                      const peername &_peer,
                      waitbox<shutdowncode> &_shutdown)
    : rpcconn(_sock, __auth, _peer),
      shutdown(_shutdown) {}
public:  messageresult message(const wireproto::rx_message &);
public:  ~pingableconn() {}
};

class pingableserver : public rpcserver {
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
    auto res(new pingableserver());
    auto rr(res->rpcserver::listen(p));
    if (rr != Success) {
        delete res;
        return rr.failure(); }
    return res; }

orerror<rpcconn *>
pingableserver::accept(socket_t s) {
    return rpcconn::fromsocket<pingableconn>(
        s,
        rpcconnauth::mkdone(),
        shutdown); }

int
main()
{
    initlogging("pingableserver");
    initpubsub();
    auto server(pingableserver::listen(peername::tcpany())
                .fatal("listening"));
    fields::print("listening on " + fields::mk(server->localname())
                  + "\n");
    auto r(server->shutdown.get(clientio::CLIENTIO));
    server->destroy(clientio::CLIENTIO);
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging();
    r.finish();
}
