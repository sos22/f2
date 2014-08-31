#ifndef SOCKET_H__
#define SOCKET_H__

#include "fd.H"

class listenfd;
class peername;
template <typename> class orerror;

class socketpairres;

class socket_t : public fd_t {
    friend class listenfd;
    friend class tcpsocket;
private: socket_t(int _fd) : fd_t(_fd) {}
public:  void operator =(const socket_t &o);
public:  orerror<peername> peer() const;
public:  peername localname() const;
public:  static orerror<listenfd> listen(const peername &);
public:  static orerror<socketpairres> socketpair();
};

class socketpairres {
    friend class socket_t;
public:  socket_t fd0;
public:  socket_t fd1;
private: socketpairres(socket_t _fd0, socket_t _fd1)
    : fd0(_fd0),
      fd1(_fd1) {}
public:  void close();
};

#endif /* !SOCKET_H__ */
