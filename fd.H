#ifndef FD_H__
#define FD_H__

#include "clientio.H"
#include "maybe.H"
#include "obj.H"
#include "orerror.H"
#include "timestamp.H"

struct pollfd;
class buffer;

template <typename __fd_t>
struct __piperes {
    __piperes() : read(), write() {}
    __fd_t read;
    __fd_t write;
};

class fd_t;

namespace fields {
class field;
const field &mk(const fd_t &);
};

class fd_t {
    friend class listenfd;
    friend class unixsocket;
    friend const fields::field &fields::mk(const fd_t &);
    friend class udpsocket;
    friend class tcpsocket;
    friend class socket_t;
    int fd;
protected:
    explicit fd_t(int _fd)
        : fd(_fd)
        {}
public:
    virtual ~fd_t() {}
    fd_t() : fd(-1) {}
    /* Construct a pollfd structure, suitable for passing off to
       poll().  events is one of 0, POLLIN, POLLOUT, or POLLIN|POLLOUT
       indicating what we should poll for. */
    struct pollfd poll(int events) const;
    /* Check whether this pollfd is for this fd. */
    bool polled(const struct pollfd &) const;
    /* Close the wrapped FD. */
    void close() const;
    /* Write some bytes into the FD.  A very thin wrapper around
     * ::write() */
    orerror<size_t> write(clientio,
                          const void *buf,
                          size_t bufsz,
                          maybe<timestamp> deadline = Nothing) const;
    /* Read some bytes from the FD.  A very thin wrapper around
     * ::read() */
    orerror<size_t> read(clientio,
                         void *buf,
                         size_t bufsz,
                         maybe<timestamp> deadline = Nothing) const;
    typedef __piperes<fd_t> piperes;
    /* Wrapper around ::pipe().  Main advantage is that the read and
     * write FDs are named, rather than being fd[0] and fd[1]. */
    static orerror<piperes> pipe();
};

#endif /* !FD_H__ */
