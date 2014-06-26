#ifndef RPCCONNTHREAD_H__
#define RPCCONNTHREAD_H__

#include "rpcconn.H"
#include "thread.H"
#include "waitbox.H"

template <typename> class rpcserver;
template <typename> class rpcservice;

template <typename cookie_t>
class rpcconnthread : threadfn {
    friend class rpcserver<cookie_t>;
public: class startconn {
        public: virtual orerror<cookie_t> operator()(
            clientio,
            rpcconn &) { return cookie_t(); };
    };
public: static startconn nostartconn;
public: class endconn {
        public: virtual void operator()(
            clientio,
            cookie_t) {};
    };
public: static endconn noendconn;
private: startconn &startconn_;
private: endconn &endconn_;
private: thread *thr_;
private: rpcservice<cookie_t> *service;
private: cookie_t ctxt;
public:  rpcconn conn;
private: waitbox<bool> shutdown; /* set under the server mux */
private: rpcconnthread(
    startconn &_startconn,
    endconn &_endconn,
    rpcservice<cookie_t> *_service,
    socket_t _fd,
    const peername &_peer)
    : startconn_(_startconn),
      endconn_(_endconn),
      thr_(NULL),
      service(_service),
      ctxt(),
      conn(_fd, _peer),
      shutdown() {}
private: void run(clientio);
private: bool runcommand(bool *die);
public:  thread *thr() const;
    /* Establish a conn thread around an already-accepted socket */
public:  static orerror<rpcconnthread<cookie_t> *> spawn(
    rpcservice<cookie_t> *service,
    startconn &,
    endconn &,
    socket_t fd,
    maybe<mutex_t::token> ); };

#endif /* !RPCCONNTHREAD_H__ */
