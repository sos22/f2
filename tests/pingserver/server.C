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
    friend class rpcconn;
public:  waitbox<shutdowncode> *shutdown;
private: pingableconn(socket_t _sock,
                      const peername &_peer)
    : rpcconn(_sock, _peer),
      shutdown(NULL) {}
public:  messageresult message(const wireproto::rx_message &);
public:  ~pingableconn() {}
};

class pingableserver : public rpcserver<pingableconn> {
public:  waitbox<shutdowncode> shutdown;
public:  static orerror<pingableserver *> listen(
    const peername &p);
private: orerror<pingableconn *> accept(socket_t sock);
private: ~pingableserver() {}
};

messageresult
pingableconn::message(const wireproto::rx_message &msg) {
    if (msg.tag() == proto::QUIT::tag) {
        auto code(msg.getparam(proto::QUIT::req::reason));
        if (code == Nothing) return error::missingparameter;
        shutdown->set(code.just());
        return messageresult::noreply;
    } else {
        return rpcconn::message(msg); } }

orerror<pingableserver *>
pingableserver::listen(const peername &p) {
    auto res(new pingableserver());
    auto rr(res->rpcserver<pingableconn>::listen(p));
    if (rr.isjust()) {
        delete res;
        return rr.just(); }
    return res; }

orerror<pingableconn *>
pingableserver::accept(socket_t s) {
    auto r(rpcconn::fromsocket<pingableconn>(s));
    if (r.issuccess()) r.success()->shutdown = &shutdown;
    return r; }

int
main()
{
    initlogging("pingableserver");
    initpubsub();
    auto server(pingableserver::listen(peername::tcpany()));
    if (server.isfailure()) server.failure().fatal("listening");
    fields::print("listening on " + fields::mk(server.success()->localname())
                  + "\n");
    auto r(server.success()->shutdown.get());
    server.success()->destroy(clientio::CLIENTIO);
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging();
    r.finish();
}

template orerror<pingableconn*> rpcconn::fromsocket<pingableconn>(socket_t);

template class rpcserver<pingableconn>;

template list<rpcserver<pingableconn>::connsub*>::list();
template bool list<rpcserver<pingableconn>::connsub*>::empty() const;
template list<rpcserver<pingableconn>::connsub*>::~list();
template void list<rpcserver<pingableconn>::connsub*>::pushtail(
    rpcserver<pingableconn>::connsub* const&);
template list<rpcserver<pingableconn>::connsub*>::iter
    list<rpcserver<pingableconn>::connsub*>::start();
template list<rpcserver<pingableconn>::connsub*>::iter::iter(
    list<rpcserver<pingableconn>::connsub*>*, bool);
template rpcserver<pingableconn>::connsub* &
    list<rpcserver<pingableconn>::connsub*>::iter::operator*();
template bool list<rpcserver<pingableconn>::connsub*>::iter::finished() const;
template void list<rpcserver<pingableconn>::connsub*>::iter::remove();
template void list<rpcserver<pingableconn>::connsub*>::iter::next();
template class either<subscriptionbase*, wireproto::rx_message const*>;
