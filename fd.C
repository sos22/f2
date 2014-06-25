#include "fd.H"

#include <sys/poll.h>
#include <unistd.h>

#include "fields.H"
#include "maybe.H"
#include "timedelta.H"

void
fd_t::close(void) const
{
    ::close(fd);
}

struct pollfd
fd_t::poll(int mode) const
{
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = mode;
    pfd.revents = 0;
    return pfd;
}

bool
fd_t::polled(const struct pollfd &pfd) const
{
    return pfd.fd == fd;
}

orerror<size_t>
fd_t::read(clientio, void *buf, size_t sz, maybe<timestamp> deadline) const {
    if (deadline != Nothing) {
        while (1) {
            struct pollfd pfd = poll(POLLIN);
            auto remaining(
                (deadline.just() - timestamp::now()).as_milliseconds());
            int r = ::poll(&pfd, 1, remaining);
            if (r < 0) return error::from_errno();
            if (r == 1) break;
            assert(r == 0); } }
    auto s(::read(fd, buf, sz));
    if (s < 0)
        return error::from_errno();
    else if (s == 0)
        return error::disconnected;
    else
        return s;
}

orerror<size_t>
fd_t::write(clientio,
            const void *buf,
            size_t sz,
            maybe<timestamp> deadline) const {
    if (deadline != Nothing) {
        while (1) {
            struct pollfd pfd = poll(POLLOUT);
            auto remaining(
                (deadline.just() - timestamp::now()).as_milliseconds());
            int r = ::poll(&pfd, 1, remaining);
            if (r < 0) return error::from_errno();
            if (r == 1) break;
            assert(r == 0); } }
    auto s(::write(fd, buf, sz));
    if (s < 0)
        return error::from_errno();
    else if (s == 0)
        return error::disconnected;
    else
        return s; }

orerror<fd_t::piperes>
fd_t::pipe()
{
    int fds[2];
    if (::pipe(fds) < 0)
        return error::from_errno();
    piperes r;
    r.read = fd_t(fds[0]);
    r.write = fd_t(fds[1]);
    return r;
}

const fields::field &
fields::mk(const fd_t &fd)
{
    return "fd:" + fields::mk(fd.fd).nosep();
}
