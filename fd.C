#include "fd.H"

#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

#include "fields.H"
#include "maybe.H"
#include "proto.H"
#include "timedelta.H"

#include "wireproto.tmpl"

#include "fieldfinal.H"

void
fd_t::close(void) const
{
    ::close(fd);
}

struct pollfd
fd_t::poll(unsigned short mode) const
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
            if (remaining < 0) return error::timeout;
            assert(remaining == (int)remaining);
            int r = ::poll(&pfd, 1, (int)remaining);
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
            assert(remaining == (int)remaining);
            int r = ::poll(&pfd, 1, (int)remaining);
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

maybe<error>
fd_t::nonblock(bool fl) const {
    int oldflags;
    oldflags = ::fcntl(fd, F_GETFL);
    if (oldflags < 0) return error::from_errno();
    if (!!(oldflags & O_NONBLOCK) == fl) return Nothing;
    if (::fcntl(fd, F_SETFL, oldflags ^ O_NONBLOCK) < 0) {
        return error::from_errno(); }
    else return Nothing; }

maybe<error>
fd_t::dup2(fd_t other) const {
    int r;
    r = ::dup2(fd, other.fd);
    if (r < 0) return error::from_errno();
    else return Nothing; }

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

fd_t::status_t
fd_t::status() const {
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

wireproto_wrapper_type(fd_t::status_t)
void
fd_t::status_t::addparam(
    wireproto::parameter<fd_t::status_t> tmpl,
    wireproto::tx_message &out) const {
    wireproto::tx_compoundparameter p;
    p.addparam(proto::fd_tstatus::fd, fd);
#define doparam(name)                                                   \
    if (name.isjust()) p.addparam(proto::fd_tstatus::name, name.just());
    fd_tstatus_params(doparam)
#undef doparam
    out.addparam(
        wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
        p); }
maybe<fd_t::status_t>
fd_t::status_t::fromcompound(const wireproto::rx_message &rxm) {
#define doparam(name)                                   \
    auto name(rxm.getparam(proto::fd_tstatus::name));
    doparam(fd);
    fd_tstatus_params(doparam);
#undef doparam
    if (!fd) return Nothing;
    return fd_t::status_t(
        fd.just(),
#define doparam1(name) name
#define doparam(name) doparam1(name),
        _fd_tstatus_params(doparam, doparam1)
#undef doparam
#undef doparam1
        ); }
const fields::field &
fields::mk(const fd_t::status_t &o) {
    return "<fd:" + mk(o.fd) +
#define doparam(name)                           \
        " " #name ":" + mk(o.name) +
        fd_tstatus_params(doparam)
#undef doparam
        ">"; }
