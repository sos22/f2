#ifndef LISTENFD_H__
#define LISTENFD_H__

#include "fd.H"
#include "orerror.H"

struct pollfd;

class listenfd {
    friend class unixsocket;
    int fd;
    listenfd(int);
public:
    listenfd() : fd(-1) {}
    orerror<fd_t> accept() const;
    struct pollfd poll() const;
    bool polled(const struct pollfd &) const;
    void close() const;
};

#endif /* !LISTENFD_H__ */
