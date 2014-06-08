#include "listenfd.H"
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stddef.h>
#include <unistd.h>

orerror<fd_t>
listenfd::accept() const
{
    int n(::accept(fd, NULL, NULL));
    if (n < 0)
        return error::from_errno();
    else
        return fd_t(n);
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
