#include "fd.H"

#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

#include "fields.H"
#include "maybe.H"
#include "proto.H"
#include "test.H"
#include "timedelta.H"

#include "maybe.tmpl"
#include "wireproto.tmpl"

#include "fieldfinal.H"

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
    else if (errno == EAGAIN) return error::wouldblock;
    else return error::from_errno(); }

orerror<size_t>
fd_t::writefast(const void *buf, size_t sz) const {
    auto s(::write(fd, buf, sz));
    if (s == 0) return error::disconnected;
    else if (s > 0) return (size_t)s;
    else if (errno == EAGAIN || errno == EWOULDBLOCK) return error::wouldblock;
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
    if (::pipe2(fds, O_CLOEXEC) < 0) return error::from_errno();
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

void
tests::fd() {
    testcaseV("fd", "statuswire", [] {
            wireproto::roundtrip<fd_t::status_t>(); });
    testcaseV("fd", "closepoll", [] {
            int p[2];
            int r(::pipe(p));
            assert(r >= 0);
            ::close(p[1]);
            fd_t fd(p[0]);
            assert(fd.poll(POLLIN).revents == 0);
            assert((fd.poll(POLLIN).events & POLLIN) != 0);
            assert((fd.poll(POLLIN).events & POLLOUT) == 0);
            assert((fd.poll(POLLOUT).events & POLLIN) == 0);
            assert((fd.poll(POLLOUT).events & POLLOUT) != 0);
            assert(fd.poll(POLLIN).fd == p[0]);
            assert(fd.polled(fd.poll(POLLIN)));
            fd.close();
            assert(write(p[0], "foo", 3) == -1);
            assert(errno == EBADF); });
    testcaseV("fd", "readwrite", [] {
            auto r(fd_t::pipe().fatal("pipe"));
            {   auto s(r.write.write(clientio::CLIENTIO, "X", 1));
                assert(s.success() == 1); }
            {   char buf[] = "Hello";
                auto s(r.read.read(clientio::CLIENTIO, buf, 5));
                assert(s.success() == 1);
                assert(!strcmp(buf, "Xello")); }
            r.read.close();
            r.write.close(); });
    testcaseV("fd", "readtimeout", [] {
            auto r(fd_t::pipe().fatal("pipe"));
            char buf[] = "Hello";
            auto s(r.read.read(
                       clientio::CLIENTIO,
                       buf,
                       5,
                       timestamp::now() + timedelta::milliseconds(5)));
            assert(s == error::timeout);
            assert(!strcmp(buf, "Hello"));
            r.read.close();
            r.write.close(); });
    testcaseV("fd", "readerr", [] {
            auto r(fd_t::pipe().fatal("pipe"));
            char buf[] = "Hello";
            r.read.close();
            auto s(r.read.read(
                       clientio::CLIENTIO,
                       buf,
                       5));
            assert(s == error::from_errno(EBADF));
            assert(!strcmp(buf, "Hello"));
            r.write.close(); });
    testcaseV("fd", "readdisconnected", [] {
            auto r(fd_t::pipe().fatal("pipe"));
            char buf[] = "Hello";
            r.write.close();
            auto s(r.read.read(
                       clientio::CLIENTIO,
                       buf,
                       5));
            assert(s == error::disconnected);
            assert(!strcmp(buf, "Hello"));
            r.read.close(); });
    testcaseV("fd", "writeerr", [] {
            auto r(fd_t::pipe().fatal("pipe"));
            char buf[] = "Hello";
            r.write.close();
            auto s(r.write.write(
                       clientio::CLIENTIO,
                       buf,
                       5));
            assert(s == error::from_errno(EBADF));
            r.read.close(); });
    testcaseV("fd", "writedisconnected", [] {
            auto r(fd_t::pipe().fatal("pipe"));
            char buf[] = "Hello";
            r.read.close();
            auto s(r.write.write(
                       clientio::CLIENTIO,
                       buf,
                       5));
            assert(s == error::from_errno(EPIPE));
            r.write.close(); });
    testcaseV("fd", "writetimeout", [] {
            auto r(fd_t::pipe().fatal("pipe"));
            while (true) {
                char buf[] = "Hello";
                auto s(r.write.write(
                           clientio::CLIENTIO,
                           buf,
                           sizeof(buf),
                           timestamp::now() + timedelta::milliseconds(5)));
                if (s == error::timeout) break;
                assert(s.success() == sizeof(buf)); }
            r.read.close();
            r.write.close(); });
    testcaseV("fd", "nonblockread", [] {
            auto r(fd_t::pipe().fatal("pipe"));
            char buf[] = "Hello";
            r.read.nonblock(true).fatal("nonblock");
            auto s(r.read.read(
                       clientio::CLIENTIO,
                       buf,
                       5));
            assert(s == error::wouldblock);
            assert(!strcmp(buf, "Hello"));
            r.read.close();
            r.write.close(); });
    testcaseV("fd", "nonblockwrite", [] {
            auto r(fd_t::pipe().fatal("pipe"));
            r.write.nonblock(true).fatal("nonblock");
            while (true) {
                char buf[] = "Hello";
                auto s(r.write.write(
                           clientio::CLIENTIO,
                           buf,
                           sizeof(buf)));
                if (s == error::wouldblock) break;
                assert(s.success() == sizeof(buf)); }
            r.close(); });
    testcaseV("fd", "status", [] {
            auto r(fd_t::pipe().fatal("pipe"));
            fields::print(fields::mk(r.read.status()) + "\n");
            fields::print(fields::mk(r.write.status()) + "\n"); });
    testcaseV("fd", "dup2", [] {
            auto r(fd_t::pipe().fatal("pipe"));
            r.write.write(clientio::CLIENTIO, "HELLO", 5);
            auto r2(fd_t::pipe().fatal("pipe"));
            r2.write.close();
            r.read.dup2(r2.read).fatal("dup2");
            r.read.close();
            char buf[5];
            auto r3(r2.read.read(clientio::CLIENTIO, buf, 5));
            assert(r3.success() == 5);
            r2.read.close();
            r.write.close(); });
    testcaseV("fd", "field", [] {
            assert(!strcmp(fields::mk(fd_t(7)).c_str(),
                           "fd:7")); });
    testcaseV("fd", "badstatus", [] {
            auto r(fd_t::pipe().fatal("pipe"));
            r.close();
            fields::print(fields::mk(r.read.status()) + "\n"); });
    testcaseV("fd", "readpoll", [] {
            auto r(fd_t::pipe().fatal("pipe"));
            char buf[10];
            assert(r.read.readpoll(buf, sizeof(buf)) == error::timeout);
            r.write.write(clientio::CLIENTIO, "HELLO", 6)
                .fatal("pipe write");
            assert(r.read.readpoll(buf, sizeof(buf)) == 6);
            assert(!strcmp(buf, "HELLO"));
            r.close(); });
}
