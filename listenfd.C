#include "listenfd.H"

#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <stddef.h>
#include <unistd.h>

#include "fields.H"

orerror<listenfd::acceptres>
listenfd::accept() const
{
    union {
        struct sockaddr s;
        struct sockaddr_in sin;
        struct sockaddr_un sun;
        struct sockaddr_in6 sin6;
    } addr;
    socklen_t addrlen(sizeof(addr));
    int n(::accept(fd, &addr.s, &addrlen));
    if (n < 0)
        return error::from_errno();
    else
        return acceptres(fd_t(n), peername(&addr.s, addrlen));
}

listenfd::listenfd(int n)
    : fd(n)
{}

struct pollfd
listenfd::poll() const
{
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    return pfd;
}

bool
listenfd::polled(const pollfd &pfd) const
{
    return pfd.fd == fd;
}

void
listenfd::close() const
{
    ::close(fd);
}

const fields::field &
fields::mk(const listenfd &fd)
{
    return "listen:" + fields::mk(fd.fd).nosep();
}
