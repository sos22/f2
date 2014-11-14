#include "socket.H"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <unistd.h>

#include "listenfd.H"
#include "peername.H"

orerror<peername>
socket_t::peer() const {
    unsigned char addr[4096];
    socklen_t addrlen(sizeof(addr));
    auto res(::getpeername(fd, (struct sockaddr *)addr, &addrlen));
    if (res < 0) return error::from_errno();
    else return peername((const struct sockaddr *)addr, addrlen); }

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

orerror<socketpairres>
socket_t::socketpair() {
    int scv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, scv) < 0) {
        return error::from_errno(); }
    return socketpairres(socket_t(scv[0]), socket_t(scv[1])); }

void
socketpairres::close() {
    fd0.close();
    fd1.close(); }

/* Set the socket options used for all of our RPC sockets.  Note that
 * this can leave some options set and other unset if it fails; pretty
 * much the only sane thing to do at that point is close() it and
 * start again. */
orerror<void>
socket_t::setsockoptions() const {
    int nodelay = 1;
    int keepalive = 1;
    int keepcnt = 10;
    int keepidle = 10;
    int keepintvl = 1;
    int usertimeout = 20;
    if (::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
                     &keepalive, sizeof(keepalive)) < 0 ||
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                     &nodelay, sizeof(nodelay)) < 0 ||
        ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT,
                     &keepcnt, sizeof(keepcnt)) < 0 ||
        ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE,
                     &keepidle, sizeof(keepidle)) < 0 ||
        ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL,
                     &keepintvl, sizeof(keepintvl)) < 0 ||
        ::setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT,
                     &usertimeout, sizeof(usertimeout)) < 0) {
        return error::from_errno(); }
    else return Success; }
