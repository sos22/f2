#include "peername.H"

#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#include "error.H"
#include "fields.H"
#include "proto.H"
#include "wireproto.H"

peername::peername(const peername &o)
    : sockaddr_(malloc(o.sockaddrsize_ + 1)),
      sockaddrsize_(o.sockaddrsize_)
{
    ((char *)sockaddr_)[sockaddrsize_] = 0;
    memcpy(sockaddr_, o.sockaddr_, o.sockaddrsize_);
}

peername::peername(const struct sockaddr *s, size_t size)
    : sockaddr_(malloc(size + 1)),
      sockaddrsize_(size)
{
    ((char *)sockaddr_)[size] = 0;
    memcpy(sockaddr_, s, size);
    switch (s->sa_family) {
    case AF_UNIX:
        assert(size <= sizeof(struct sockaddr_un));
        break;
    case AF_INET:
        assert(size == sizeof(struct sockaddr_in));
        break;
    case AF_INET6:
        assert(size == sizeof(struct sockaddr_in6));
        break;
    default:
        break;
    }
}

peername::~peername()
{
    free(sockaddr_);
}

const fields::field &
fields::mk(const peername &p) {
    auto sa(p.sockaddr());
    switch (sa->sa_family) {
    case AF_UNIX:
        return "unix://" +
            fields::mk(((const struct sockaddr_un *)sa)->sun_path) +
            "/";
    case AF_INET: {
        auto addr((const struct sockaddr_in *)sa);
        char buf[INET_ADDRSTRLEN];
        auto r(inet_ntop(sa->sa_family, &addr->sin_addr, buf, sizeof(buf)));
        return "ip://" +
            (r
             ? fields::mk(buf)
             : "<error " +
               fields::mk(error::from_errno()) +
               " formatting peer address>") +
            ":" +
            fields::mk(htons(addr->sin_port)).nosep() +
            "/";
    }
    case AF_INET6: {
        auto addr((const struct sockaddr_in6 *)sa);
        char buf[INET6_ADDRSTRLEN];
        auto r(inet_ntop(sa->sa_family, &addr->sin6_addr, buf, sizeof(buf)));
        return "ip6://" +
            (r
             ? fields::mk(buf)
             : "<error " +
               fields::mk(error::from_errno()) +
               " formatting peer address>") +
            ":" +
            fields::mk(htons(addr->sin6_port)).nosep() +
            "/";
    }
    default:
        return "<unknown address family " + fields::mk(sa->sa_family);
    }
}

void
peername::addparam(wireproto::parameter<peername> tmpl,
                   wireproto::tx_message &tx_msg) const
{
    wireproto::tx_compoundparameter tx;
    auto sa(sockaddr());
    const char *c;
    switch (sa->sa_family) {
    case AF_UNIX:
        c = ((const struct sockaddr_un *)sa)->sun_path;
        tx.addparam(proto::peername::local, c);
        break;
    case AF_INET:
        tx.addparam(proto::peername::ipv4,
                    *(uint32_t *)&((const struct sockaddr_in *)sa)->sin_addr);
        tx.addparam(proto::peername::port,
                    ((const struct sockaddr_in *)sa)->sin_port);
        break;
    case AF_INET6:
        tx.addparam(
            proto::peername::ipv6a,
            ((uint64_t *)&((const struct sockaddr_in6 *)sa)->sin6_addr)[0]);
        tx.addparam(
            proto::peername::ipv6b,
            ((uint64_t *)&((const struct sockaddr_in6 *)sa)->sin6_addr)[1]);
        tx.addparam(proto::peername::port,
                    ((const struct sockaddr_in6 *)sa)->sin6_port);
        break;
    default:
        abort();
    }
    tx_msg.addparam(
        wireproto::parameter<wireproto::tx_compoundparameter>(tmpl), tx);
}
maybe<peername>
peername::getparam(wireproto::parameter<peername> tmpl,
                   const wireproto::rx_message &msg)
{
    auto packed(msg.getparam(
               wireproto::parameter<wireproto::rx_compoundparameter>(tmpl)));
    if (!packed) return Nothing;
    auto local(packed.just().getparam(proto::peername::local));
    if (local.isjust()) {
        struct sockaddr_un un;
        memset(&un, 0, sizeof(un));
        if (strlen(local.just()) >= sizeof(un.sun_path)) return Nothing;
        un.sun_family = AF_UNIX;
        strcpy(un.sun_path, local.just());
        return peername((struct sockaddr *)&un, sizeof(un));
    }
    auto port(packed.just().getparam(proto::peername::port));
    if (port == Nothing) return Nothing;
    auto ipv4(packed.just().getparam(proto::peername::ipv4));
    if (ipv4.isjust()) {
        struct sockaddr_in in;
        memset(&in, 0, sizeof(in));
        in.sin_family = AF_INET;
        in.sin_port = port.just();
        *(uint32_t *)&in.sin_addr = ipv4.just();
        return peername((struct sockaddr *)&in, sizeof(in));
    }
    auto ipv6a(packed.just().getparam(proto::peername::ipv6a));
    auto ipv6b(packed.just().getparam(proto::peername::ipv6b));
    if (!ipv6a || !ipv6b)
        return Nothing;
    struct sockaddr_in6 in6;
    memset(&in6, 0, sizeof(in6));
    in6.sin6_family = AF_INET6;
    in6.sin6_port = port.just();
    ((uint64_t *)&in6.sin6_addr)[0] = ipv6a.just();
    ((uint64_t *)&in6.sin6_addr)[1] = ipv6b.just();
    return peername((struct sockaddr *)&in6, sizeof(in6));
}

peername::port::port(int _p)
    : p(_p) {}

peername
peername::udpbroadcast(peername::port p)
{
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(p.p);
    memset(&sin.sin_addr, 0xff, sizeof(sin.sin_addr));
    return peername((const struct sockaddr *)&sin, sizeof(sin));
}

peername
peername::tcpany()
{
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    return peername((const struct sockaddr *)&sin, sizeof(sin));
}

peername
peername::local(const char *path) {
    struct sockaddr_un sun;
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    assert(strlen(path) < sizeof(sun.sun_path));
    strcpy(sun.sun_path, path);
    return peername((const struct sockaddr *)&sun, sizeof(sun)); }

bool
peername::samehost(const peername &o) const {
    auto us(sockaddr());
    auto them(o.sockaddr());
    if (us->sa_family != them->sa_family) return false;
    switch (us->sa_family) {
    case PF_LOCAL: return true;
    case PF_INET: {
        auto in_us((const struct sockaddr_in *)us);
        auto in_them((const struct sockaddr_in *)them);
        return in_us->sin_addr.s_addr == in_them->sin_addr.s_addr; }
    case PF_INET6: {
        auto in6_us((const struct sockaddr_in6 *)us);
        auto in6_them((const struct sockaddr_in6 *)them);
        return !memcmp(in6_us->sin6_addr.s6_addr,
                       in6_them->sin6_addr.s6_addr,
                       sizeof(in6_us->sin6_addr.s6_addr)); }
    default:
        abort(); } }

const struct sockaddr *
peername::sockaddr() const {
    return (const struct sockaddr *)sockaddr_; }

size_t
peername::sockaddrsize() const {
    return sockaddrsize_; }
