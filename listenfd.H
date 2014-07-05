#ifndef LISTENFD_H__
#define LISTENFD_H__

#include "socket.H"

struct pollfd;

class listenfd;

namespace fields {
class field;
const field &mk(const listenfd &);
};

/* A file descriptor for listening on, rather than performing normal
   IO on. */
class listenfd {
    friend const fields::field &fields::mk(const listenfd &);
    friend class socket_t;
    int fd;
    explicit listenfd(int);
public:
    listenfd() : fd(-1) {}
    /* Accept the next incoming connection and construct a data
     * socket_t for it. */
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
};

#endif /* !LISTENFD_H__ */
