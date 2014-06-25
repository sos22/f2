#ifndef RPCCONNTHREAD_H__
#define RPCCONNTHREAD_H__

template <typename cookie_t>
class rpcconnthread : threadfn {
    friend class rpcserver<cookie_t>;
private: thread *thr_;
private: rpcservice<cookie_t> *service;
private: waitbox<cookie_t> ctxt;
private: rpcconn conn;
private: waitbox<bool> shutdown; /* set under the server mux */
private: rpcconnthread(
    rpcservice<cookie_t> *_service,
    socket_t _fd,
    const peername &_peer)
    : thr_(NULL),
      service(_service),
      ctxt(),
      conn(_fd, _peer),
      shutdown() {}
private: void run();
private: bool runcommand(bool *die);
public:  thread *thr() const;
public:  static orerror<rpcconnthread<cookie_t> *> spawn(
    rpcserver<cookie_t> *owner,
    socket_t fd,
    waitbox<bool> &);
};

#endif /* !RPCCONNTHREAD_H__ */
