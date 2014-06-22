#ifndef SOCKET_H__
#define SOCKET_H__

#include "fd.H"

class listenfd;
class peername;
template <typename> class orerror;

class socket_t : public fd_t {
    friend class listenfd;
    friend class tcpsocket;
private: socket_t(int _fd) : fd_t(_fd) {}
public:  void operator =(const socket_t &o);
public:  orerror<peername> peer() const;
public:  peername localname() const;
public:  static orerror<listenfd> listen(const peername &);
};

#endif /* !SOCKET_H__ */
