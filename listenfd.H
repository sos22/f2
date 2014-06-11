#ifndef LISTENFD_H__
#define LISTENFD_H__

#include "fd.H"
#include "orerror.H"
#include "peername.H"

struct pollfd;

/* A file descriptor for listening on, rather than performing normal
   IO on. */
class listenfd {
    friend class unixsocket;
    int fd;
    explicit listenfd(int);
public:
    listenfd() : fd(-1) {}
    /* Accept the next incoming connection and construct a data fd_t
     * for it. */
    struct acceptres {
        acceptres(const fd_t &_fd, const peername &_peer)
            : fd(_fd), peer(_peer)
            {}
        fd_t fd;
        peername peer;
    };
    orerror<acceptres> accept() const;
    /* Generate a struct pollfd which, when passed to poll(), will
     * cause us to wait for incoming connections. */
    struct pollfd poll() const;
    /* Check whether this pollfd is for this file descriptor. */
    bool polled(const struct pollfd &) const;
    /* Close the underlying fd */
    void close() const;
};

#endif /* !LISTENFD_H__ */
