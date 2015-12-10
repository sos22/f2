#ifndef LISTENFD_H__
#define LISTENFD_H__

#include "peername.H"
#include "socket.H"

namespace fields { class field; }
struct pollfd;

class listenfd;
class listenfdstatus;

/* A file descriptor for listening on, rather than performing normal
   IO on. */
class listenfd {
    friend class socket_t;
    int fd;
public:
    explicit listenfd(int);
    listenfd() : fd(-1) {}
    /* Accept the next incoming connection and construct a data
     * socket_t for it. */
    orerror<socket_t> accept(clientio) const;
    /* Non-blocking version of accept. */
    orerror<socket_t> accept() const;
    /* Get the address on which we're listening. */
    peername localname() const;
    /* Generate a struct pollfd which, when passed to poll(), will
     * cause us to wait for incoming connections. */
    struct pollfd poll() const;
    /* Check whether this pollfd is for this file descriptor. */
    bool polled(const struct pollfd &) const;
    /* Close the underlying fd */
    void close() const;
    /* Debug status interface */
    typedef listenfdstatus status_t;
    status_t status() const;
public:  const fields::field &field() const; };

class listenfdstatus {
    friend class listenfd;
private: maybe<peername> listenon;
private: int fd;
private: maybe<int> domain;
private: maybe<int> protocol;
private: maybe<int> flags;
private: maybe<int> revents;
private: listenfdstatus(const maybe<peername> &,
                        int,
                        maybe<int>,
                        maybe<int>,
                        maybe<int>,
                        maybe<int>);
public:  listenfdstatus(quickcheck &q);
public:  bool operator==(const listenfdstatus &o) const;
public:  const fields::field &field() const; };

#endif /* !LISTENFD_H__ */
