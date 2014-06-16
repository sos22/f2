#include "socket.H"

#include <sys/socket.h>
#include <string.h>
#include <unistd.h>

#include "listenfd.H"
#include "peername.H"

peername
socket_t::peer() const {
    unsigned char addr[4096];
    socklen_t addrlen(sizeof(addr));
    auto res(::getpeername(fd, (struct sockaddr *)addr, &addrlen));
    if (res < 0) error::from_errno().fatal("getting socket local name");
    return peername((const struct sockaddr *)addr, addrlen); }

peername
socket_t::localname() const {
    unsigned char addr[4096];
    socklen_t addrlen(sizeof(addr));
    auto res(::getsockname(fd, (struct sockaddr *)addr, &addrlen));
    if (res < 0) error::from_errno().fatal("getting socket peer name");
    return peername((const struct sockaddr *)addr, addrlen); }

orerror<listenfd>
socket_t::listen(
    const peername &p) {
    auto sa(p.sockaddr());
    int fd = socket(sa->sa_family, SOCK_STREAM, 0);
    if (fd < 0 ||
        ::bind(fd, sa, p.sockaddrsize()) ||
        ::listen(fd, 20) < 0) {
        if (fd >= 0) ::close(fd);
        return error::from_errno(); }
    return listenfd(fd); }
