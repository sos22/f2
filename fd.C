#include "fd.H"

#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

#include "fields.H"
#include "quickcheck.H"
#include "timedelta.H"
#include "timestamp.H"

#include "fields.tmpl"
#include "orerror.tmpl"

void
fd_t::close(void) const
{
    ::close(fd);
}

struct pollfd
fd_t::poll(short mode) const
{
    struct pollfd pfd;
    pfd.fd = fd;
    /* Always poll for NVAL, ERR, and HUP, regardless of what the
       caller might want, because that makes the iosubscription thread
       quite a bit easier. */
    pfd.events = (short)(mode | POLLNVAL | POLLERR | POLLHUP);
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
            if (remaining < 0) return error::timeout;
            assert(remaining == (int)remaining);
            int r = ::poll(&pfd, 1, (int)remaining);
            if (r < 0) return error::from_errno();
            if (r == 1) break;
            assert(r == 0); } }
    auto s(::read(fd, buf, sz));
    if (s < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return error::wouldblock;
        else return error::from_errno(); }
    else if (s == 0) return error::disconnected;
    else return (size_t)s; }

orerror<size_t>
fd_t::readpoll(void *buf, size_t sz) const {
    return read(clientio::CLIENTIO, buf, sz, timestamp::now()); }

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
            assert(remaining == (int)remaining);
            if (remaining < 0) return error::timeout;
            int r = ::poll(&pfd, 1, (int)remaining);
            if (r < 0) return error::from_errno();
            if (r == 1) break;
            assert(r == 0); } }
    auto s(::write(fd, buf, sz));
    assert(s != 0);
    if (s > 0) return (size_t)s;
    else if (errno == EAGAIN || errno == EWOULDBLOCK) return error::wouldblock;
    else if (errno == EPIPE || errno == ECONNRESET) return error::disconnected;
    else return error::from_errno(); }

orerror<size_t>
fd_t::writefast(const void *buf, size_t sz) const {
    auto s(::write(fd, buf, sz));
    if (s == 0) return error::disconnected;
    else if (s > 0) return (size_t)s;
    else if (errno == EAGAIN || errno == EWOULDBLOCK) return error::wouldblock;
    else if (errno == EPIPE || errno == ECONNRESET) return error::disconnected;
    else return error::from_errno(); }

orerror<void>
fd_t::nonblock(bool fl) const {
    int oldflags;
    oldflags = ::fcntl(fd, F_GETFL);
    if (oldflags < 0) return error::from_errno();
    if (!!(oldflags & O_NONBLOCK) == fl) return Success;
    if (fcntl(fd, F_SETFL, oldflags^O_NONBLOCK) < 0) return error::from_errno();
    else return Success; }

orerror<void>
fd_t::dup2(fd_t other) const {
    int r;
    r = ::dup2(fd, other.fd);
    if (r < 0) return error::from_errno();
    else return Success; }

orerror<fd_t::piperes>
fd_t::pipe()
{
    int fds[2];
    if (::pipe(fds) < 0) return error::from_errno();
    piperes r;
    r.read = fd_t(fds[0]);
    r.write = fd_t(fds[1]);
    return r;
}

bool
fd_t::operator==(fd_t o) const { return fd == o.fd; }

fd_t::status_t
fd_t::status() const {
    /* XXX collect TCP_INFO as well? */
#define doparam(name) maybe<int> name(Nothing);
    fd_tstatus_params(doparam);
#undef doparam
#define sockopt(name, level, opt)                                       \
    {   int value;                                                      \
        socklen_t optlen(sizeof(value));                                \
        if (getsockopt(fd, (level), (opt), &value, &optlen) >= 0 &&     \
            optlen == sizeof(value)) name = value; }
    sockopt(domain, SOL_SOCKET, SO_DOMAIN);
    sockopt(protocol, SOL_SOCKET, SO_PROTOCOL);
    sockopt(rcvbuf, SOL_SOCKET, SO_RCVBUF);
    sockopt(sndbuf, SOL_SOCKET, SO_SNDBUF);
#undef sockopt
    {   int value;
        if (ioctl(fd, FIONREAD, &value) >= 0) rcvqueue = value;
        else error::from_errno().warn("FIONREAD"); }
    {   int value;
        if (ioctl(fd, TIOCOUTQ, &value) >= 0) sndqueue = value;
        else error::from_errno().warn("TIOCOUTQ"); }
    {   int value;
        value = ::fcntl(fd, F_GETFL);
        if (value >= 0) flags = value; }
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = ~0;
    pfd.revents = 0;
    if (::poll(&pfd, 1, 0) >= 0) revents = pfd.revents;
    return status_t(
        fd,
#define doparam1(name) name
#define doparam(name) doparam1(name),
        _fd_tstatus_params(doparam, doparam1)
#undef doparam
#undef doparam1
        ); }

const fields::field &
fd_t::field() const { return "fd:" + fields::mk(fd).nosep(); }

fd_tstatus::fd_tstatus(const quickcheck &q)
    : fd((unsigned short)q),
      domain(q),
      protocol(q),
      flags(q),
      sndbuf(q),
      rcvbuf(q),
      sndqueue(q),
      rcvqueue(q),
      revents(q) {}

bool
fd_tstatus::operator==(const fd_tstatus &o) const {
    if (fd != o.fd) return false;
#define iter(x) if (x != o.x) return false;
    fd_tstatus_params(iter);
#undef iter
    return true; }

const fields::field &
fields::mk(const fd_t::status_t &o) {
    return "<fd:" + mk(o.fd) +
#define doparam(name)                           \
        " " #name ":" + mk(o.name) +
        fd_tstatus_params(doparam)
#undef doparam
        ">"; }

template <> const fields::field &
__piperes<fd_t>::field() const {
    return "<pipe: read:" + fields::mk(read) + " write:"
        + fields::mk(write) + ">"; }
