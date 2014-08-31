#include "listenfd.H"

#include <sys/fcntl.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <arpa/inet.h>

#include "fields.H"
#include "logging.H"
#include "peername.H"
#include "proto.H"
#include "socket.H"
#include "wireproto.H"

#include "maybe.tmpl"
#include "wireproto.tmpl"

orerror<socket_t>
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
        return socket_t(n);
}

peername
listenfd::localname() const {
    unsigned char addr[4096];
    socklen_t addrlen(sizeof(addr));
    auto res(::getsockname(fd, (struct sockaddr *)addr, &addrlen));
    if (res < 0) error::from_errno().fatal("getting socket peer name");
    return peername((const struct sockaddr *)addr, addrlen).canonicalise(); }

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

listenfdstatus::listenfdstatus(quickcheck q)
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

void
listenfd::status_t::addparam(
    wireproto::parameter<listenfd::status_t> tmpl,
    wireproto::tx_message &out) const {
    wireproto::tx_compoundparameter p;
    p.addparam(proto::listenfdstatus::fd, fd);
    if (listenon != Nothing) {
        p.addparam(proto::listenfdstatus::listenon, listenon.just()); }
    if (domain != Nothing) {
        p.addparam(proto::listenfdstatus::domain, domain.just()); }
    if (protocol != Nothing) {
        p.addparam(proto::listenfdstatus::protocol, protocol.just()); }
    if (flags != Nothing) {
        p.addparam(proto::listenfdstatus::flags, flags.just()); }
    if (revents != Nothing) {
        p.addparam(proto::listenfdstatus::revents, revents.just()); }
    out.addparam(
        wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
        p); }

maybe<listenfd::status_t>
listenfd::status_t::fromcompound(const wireproto::rx_message &rxm) {
    auto listenon(rxm.getparam(proto::listenfdstatus::listenon));
    auto fd(rxm.getparam(proto::listenfdstatus::fd));
    auto domain(rxm.getparam(proto::listenfdstatus::domain));
    auto protocol(rxm.getparam(proto::listenfdstatus::protocol));
    auto flags(rxm.getparam(proto::listenfdstatus::flags));
    auto revents(rxm.getparam(proto::listenfdstatus::revents));
    if (!fd) return Nothing;
    return listenfd::status_t(listenon,
                              fd.just(),
                              domain,
                              protocol,
                              flags,
                              revents); }

wireproto_wrapper_type(listenfd::status_t)

const fields::field &
fields::mk(const listenfd::status_t &o) {
    return "<listenfd: listenon:" + mk(o.listenon) +
        " fd:" + mk(o.fd) +
        " domain:" + mk(o.domain) +
        " protocol:" + mk(o.protocol) +
        " flags:" + mk(o.flags) +
        " revents:" + mk(o.revents) +
        ">"; }
