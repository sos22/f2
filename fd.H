#ifndef FD_H__
#define FD_H__

#include "maybe.H"
#include "obj.H"
#include "orerror.H"

struct pollfd;
class buffer;

template <typename __fd_t>
struct __piperes {
    __fd_t read;
    __fd_t write;
};

class fd_t : public obj {
    friend class listenfd;
    friend class unixsocket;
    int fd;
protected:
    explicit fd_t(int _fd)
	: fd(_fd)
	{}
public:
    fd_t() : fd(-1) {}
    struct pollfd poll(int events) const;
    bool polled(const struct pollfd &) const;
    void close() const;
    orerror<size_t> write(const void *buf, size_t bufsz) const;
    orerror<size_t> read(void *buf, size_t bufsz) const;
    typedef __piperes<fd_t> piperes;
    static orerror<piperes> pipe();
};

#endif /* !FD_H__ */
