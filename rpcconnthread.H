#ifndef RPCCONNTHREAD_H__
#define RPCCONNTHREAD_H__

template <typename cookie_t>
class rpcconnthread : threadfn {
    friend class rpcserver<cookie_t>;
private: thread *thr_;
private: rpcserver<cookie_t> *owner;
private: waitbox<cookie_t> ctxt;
private: rpcconn conn;
private: waitbox<bool> &shutdownall;
private: waitbox<bool> shutdownone;
private: rpcconnthread(
    rpcserver<cookie_t> *_owner,
    socket_t _fd,
    const peername &_peer,
    waitbox<bool> &_shutdown)
    : thr_(NULL),
      owner(_owner),
      ctxt(),
      conn(_fd, _peer),
      shutdownall(_shutdown),
      shutdownone() {}
private: void run();
private: bool runcommand(bool *die);
public:  thread *thr() const;
public:  static orerror<rpcconnthread<cookie_t> *> spawn(
    rpcserver<cookie_t> *owner,
    socket_t fd,
    waitbox<bool> &);
};

#endif /* !RPCCONNTHREAD_H__ */
