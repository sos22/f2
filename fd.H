#ifndef FD_H__
#define FD_H__

#include "clientio.H"
#include "maybe.H"
#include "obj.H"
#include "orerror.H"
#include "timestamp.H"
#include "wireproto.H"

struct pollfd;
class buffer;

template <typename __fd_t>
struct __piperes {
    __piperes() : read(), write() {}
    __fd_t read;
    __fd_t write;
    void close() { read.close(); write.close(); }
};

class fd_t;

namespace fields {
class field;
const field &mk(const fd_t &);
};

class fd_tstatus {
public: int fd;
#define _fd_tstatus_params(iter, iter1) \
    iter(domain)                        \
    iter(protocol)                      \
    iter(flags)                         \
    iter(sndbuf)                        \
    iter(rcvbuf)                        \
    iter(sndqueue)                      \
    iter(rcvqueue)                      \
    iter1(revents)
#define fd_tstatus_params(iter) _fd_tstatus_params(iter, iter)
#define doparam(name) public: maybe<int> name;
    fd_tstatus_params(doparam);
#undef doparam
public: fd_tstatus(
    int _fd,
#define doparam1(name) maybe<int> _ ## name
#define doparam(name) doparam1(name),
    _fd_tstatus_params(doparam, doparam1)
#undef doparam
#undef doparam1
    )
    : fd(_fd),
#define doparam1(name) name(_##name)
#define doparam(name) doparam1(name),
    _fd_tstatus_params(doparam, doparam1)
#undef doparam
#undef doparam1
        {}
WIREPROTO_TYPE(fd_tstatus);
};
namespace fields {
const field &mk(const fd_tstatus &); }

class fd_t {
    friend class listenfd;
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
    struct pollfd poll(unsigned short events) const;
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
    /* Set the O_NONBLOCK flag on this FD. */
    maybe<error> nonblock(bool) const;
    /* Use ::dup2() to create a duplicate of this file descriptor over
     * the top of an existing FD. */
    maybe<error> dup2(fd_t) const;
    typedef __piperes<fd_t> piperes;
    /* Wrapper around ::pipe().  Main advantage is that the read and
     * write FDs are named, rather than being fd[0] and fd[1]. */
    static orerror<piperes> pipe();

public: typedef fd_tstatus status_t;
public: status_t status() const;
};

#endif /* !FD_H__ */
