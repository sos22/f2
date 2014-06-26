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
            rpcconn &conn) = 0;
    };
public: class endconn {
        public: virtual void operator()(
            clientio,
            cookie_t) = 0;
    };
private: startconn &startconn_;
private: endconn &endconn_;
private: thread *thr_;
private: rpcservice<cookie_t> *service;
private: cookie_t ctxt;
private: rpcconn conn;
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
public:  static orerror<rpcconnthread<cookie_t> *> spawn(
    rpcserver<cookie_t> *owner,
    startconn &,
    endconn &,
    socket_t fd,
    mutex_t::token /* server mux */);
};

#endif /* !RPCCONNTHREAD_H__ */
