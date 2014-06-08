#ifndef FD_H__
#define FD_H__

#include "maybe.H"
#include "obj.H"
#include "orerror.H"

struct pollfd;
class buffer;

template <typename __fd_t>
struct __piperes {
    __piperes() : read(), write() {}
    __fd_t read;
    __fd_t write;
};

class fd_t {
    friend class listenfd;
    friend class unixsocket;
    int fd;
protected:
    explicit fd_t(int _fd)
	: fd(_fd)
	{}
public:
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
    orerror<size_t> write(const void *buf, size_t bufsz) const;
    /* Read some bytes from the FD.  A very thin wrapper around
     * ::read() */
    orerror<size_t> read(void *buf, size_t bufsz) const;
    typedef __piperes<fd_t> piperes;
    /* Wrapper around ::pipe().  Main advantage is that the read and
     * write FDs are named, rather than being fd[0] and fd[1]. */
    static orerror<piperes> pipe();
};

#endif /* !FD_H__ */
