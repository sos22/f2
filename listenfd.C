#include "listenfd.H"

#include <sys/fcntl.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "fields.H"
#include "logging.H"
#include "peername.H"
#include "quickcheck.H"
#include "socket.H"

#include "fields.tmpl"
#include "maybe.tmpl"
#include "orerror.tmpl"

orerror<socket_t>
listenfd::accept(clientio) const
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
        return socket_t(n);
}

orerror<socket_t>
listenfd::accept() const {
    int n(::accept4(fd, NULL, NULL, SOCK_NONBLOCK));
    if (n >= 0) {
        socket_t res(n);
        auto r(res.setsockoptions());
        if (r.isfailure()) {
            res.close();
            return r.failure(); }
        else return res; }
    else if (errno == EAGAIN || errno == EWOULDBLOCK) return error::wouldblock;
    else return error::from_errno(); }

peername
listenfd::localname() const {
    unsigned char addr[4096];
    socklen_t addrlen(sizeof(addr));
    auto res(::getsockname(fd, (struct sockaddr *)addr, &addrlen));
    if (res < 0) error::from_errno().fatal("getting socket peer name");
    return peername((const struct sockaddr *)addr, addrlen); }

listenfd::listenfd(int n)
    : fd(n)
{}

struct pollfd
listenfd::poll() const
{
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN | POLLERR | POLLHUP | POLLNVAL;
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
listenfd::field() const
{
    return "listen:" + fields::mk(fd).nosep();
}

listenfd::status_t
listenfd::status() const {
    maybe<peername> l(Nothing);
    maybe<int> domain(Nothing);
    maybe<int> protocol(Nothing);
    maybe<int> revents(Nothing);
    maybe<int> flags(Nothing);
#define sockopt(name, level, opt)                                       \
    {   int value;                                                      \
        socklen_t optlen(sizeof(value));                                \
        if (getsockopt(fd, (level), (opt), &value, &optlen) >= 0 &&     \
            optlen == sizeof(value)) name = value; }
    sockopt(domain, SOL_SOCKET, SO_DOMAIN);
    sockopt(protocol, SOL_SOCKET, SO_PROTOCOL);
    {   int value;
        value = ::fcntl(fd, F_GETFL);
        if (value >= 0) flags = value; }
    {   struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = ~0;
        pfd.revents = 0;
        if (::poll(&pfd, 1, 0) >= 0) revents = pfd.revents; }
    {   union {
            unsigned char buf[4096];
            struct sockaddr sa;
        } u;
        socklen_t len(sizeof(u));
        if (::getsockname(fd, &u.sa, &len) == 0) l = peername(&u.sa, len); }
    return status_t(l, fd, domain, protocol, revents, flags); }

listenfdstatus::listenfdstatus(const maybe<peername> &_listenon,
                               int _fd,
                               maybe<int> _domain,
                               maybe<int> _protocol,
                               maybe<int> _flags,
                               maybe<int> _revents)
    : listenon(_listenon),
      fd(_fd),
      domain(_domain),
      protocol(_protocol),
      flags(_flags),
      revents(_revents) {}

listenfdstatus::listenfdstatus(quickcheck &q)
    : listenon(q),
      fd(q),
      domain(q),
      protocol(q),
      flags(q),
      revents(q) {}

bool
listenfdstatus::operator==(const listenfdstatus &o) const {
    return listenon == o.listenon &&
        fd == o.fd &&
        domain == o.domain &&
        protocol == o.protocol &&
        flags == o.flags &&
        revents == o.revents; }

const fields::field &
listenfd::status_t::field() const {
    return "<listenfd: listenon:" + fields::mk(listenon) +
        " fd:" + fields::mk(fd) +
        " domain:" + fields::mk(domain) +
        " protocol:" + fields::mk(protocol) +
        " flags:" + fields::mk(flags) +
        " revents:" + fields::mk(revents) +
        ">"; }
