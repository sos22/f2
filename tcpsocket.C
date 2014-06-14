#include "tcpsocket.H"

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "orerror.H"
#include "peername.H"

orerror<fd_t>
tcpsocket::connect(const peername &p)
{
    auto sa(p.sockaddr());
    int sock = ::socket(sa->sa_family, SOCK_STREAM, 0);
    if (sock < 0) return error::from_errno();
    int r = ::connect(sock, sa, p.sockaddrsize());
    if (r < 0) {
        ::close(sock);
        return error::from_errno(); }
    return fd_t(sock);
}
