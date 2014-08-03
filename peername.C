#include "peername.H"

#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

#include "either.H"
#include "error.H"
#include "fields.H"
#include "parsers.H"
#include "proto.H"
#include "test.H"
#include "util.H"
#include "wireproto.H"

#include "either.tmpl"
#include "parsers.tmpl"
#include "wireproto.tmpl"

#include "fieldfinal.H"

peername::peername(const quickcheck &q) {
    switch (random() % 3) {
    case 0: {
        struct sockaddr_un *sun = (struct sockaddr_un *)calloc(sizeof(*sun), 1);
        sockaddr_ = sun;
        sockaddrsize_ = sizeof(*sun);
        sun->sun_family = AF_UNIX;
        if (random() % 2 == 0) {
            sockaddrsize_ = sizeof(sa_family_t); }
        else {
            const char *p;
            do {
                p = q.filename();
            } while (strlen(p) >= sizeof(sun->sun_path));
            strcpy(sun->sun_path, p); }
        break; }
    case 1: {
        struct sockaddr_in *sin = (struct sockaddr_in *)calloc(sizeof(*sin), 1);
        sockaddr_ = sin;
        sockaddrsize_ = sizeof(*sin);
        sin->sin_family = AF_INET;
        switch (random() % 4) {
        case 0:
            sin->sin_addr.s_addr = INADDR_ANY;
            break;
        case 1:
            sin->sin_addr.s_addr = INADDR_BROADCAST;
            break;
        case 2:
            sin->sin_addr.s_addr = INADDR_LOOPBACK;
            break;
        case 3:
            sin->sin_addr.s_addr = (uint32_t)random();
            break; }
        sin->sin_port = (unsigned short)q;
        break; }
    case 2: {
        struct sockaddr_in6 *sin6 =
            (struct sockaddr_in6 *)calloc(sizeof(*sin6), 1);
        sockaddr_ = sin6;
        sockaddrsize_ = sizeof(*sin6);
        sin6->sin6_family = AF_INET6;
        switch (random() % 3) {
        case 0:
            sin6->sin6_addr = in6addr_any;
            break;
        case 1:
            sin6->sin6_addr = in6addr_loopback;
            break;
        case 2:
            for (unsigned x = 0; x < 4; x++) {
                ((unsigned *)sin6->sin6_addr.s6_addr)[x] = (unsigned)random(); }
            break; }
        sin6->sin6_port = (unsigned short)q;
        break; } } }

peername::peername(const peername &o)
    : sockaddr_(malloc(o.sockaddrsize_ + 1)),
      sockaddrsize_(o.sockaddrsize_)
{
    ((char *)sockaddr_)[sockaddrsize_] = 0;
    memcpy(sockaddr_, o.sockaddr_, o.sockaddrsize_);
}

peername::peername(const struct sockaddr *s, unsigned size)
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
    }
}

peername::~peername()
{
    free(sockaddr_);
}

bool
peername::operator==(const peername &o) const {
    return sockaddrsize_ == o.sockaddrsize_ &&
        !memcmp(sockaddr_, o.sockaddr_, sockaddrsize_); }

const fields::field &
fields::mk(const peername &p) {
    auto sa(p.sockaddr());
    switch (sa->sa_family) {
    case AF_UNIX:
        if (p.sockaddrsize() == sizeof(sa_family_t)) {
            return fields::mk("unix*:///"); }
        else {
            return "unix://" +
            fields::mk(((const struct sockaddr_un *)sa)->sun_path).escape() +
                "/"; }
    case AF_INET: {
        auto addr((const struct sockaddr_in *)sa);
        return "ip://" +
            fields::mk((addr->sin_addr.s_addr >> 24) & 0xff) + "." +
            fields::mk((addr->sin_addr.s_addr >> 16) & 0xff) + "." +
            fields::mk((addr->sin_addr.s_addr >> 8) & 0xff) + "." +
            fields::mk((addr->sin_addr.s_addr >> 0) & 0xff) + ":" +
            fields::mk(htons(addr->sin_port)).nosep() + "/"; }
    case AF_INET6: {
        auto addr((const struct sockaddr_in6 *)sa);
        char buf[INET6_ADDRSTRLEN];
        auto r(inet_ntop(sa->sa_family, &addr->sin6_addr, buf, sizeof(buf)));
        assert(r == buf);
        return "ip6://[" + fields::mk(buf) + "]:" +
            fields::mk(htons(addr->sin6_port)).nosep() + "/";
    }
    default:
        return "<unknown address family " + fields::mk(sa->sa_family);
    }
}

const fields::field &
fields::mk(const peername::port &p) {
    return "<port:" + mk(p.unwrap()) + ">"; }

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
        if (sockaddrsize() == sizeof(sa_family_t))
            tx.addparam(proto::peername::wildcardlocal, true);
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
#ifndef COVERAGESKIP
        abort();
#endif
    }
    tx_msg.addparam(
        wireproto::parameter<wireproto::tx_compoundparameter>(tmpl), tx);
}
maybe<peername>
peername::fromcompound(const wireproto::rx_message &msg)
{
    auto local(msg.getparam(proto::peername::local));
    if (local.isjust()) {
        struct sockaddr_un un;
        memset(&un, 0, sizeof(un));
        if (strlen(local.just()) >= sizeof(un.sun_path)) return Nothing;
        un.sun_family = AF_UNIX;
        strcpy(un.sun_path, local.just());
        return peername(
            (struct sockaddr *)&un,
            msg.getparam(proto::peername::wildcardlocal).dflt(false)
            ? sizeof(sa_family_t)
            : sizeof(un)); }
    auto port(msg.getparam(proto::peername::port));
    if (port == Nothing) return Nothing;
    auto ipv4(msg.getparam(proto::peername::ipv4));
    if (ipv4.isjust()) {
        struct sockaddr_in in;
        memset(&in, 0, sizeof(in));
        in.sin_family = AF_INET;
        in.sin_port = port.just();
        *(uint32_t *)&in.sin_addr = ipv4.just();
        return peername((struct sockaddr *)&in, sizeof(in));
    }
    auto ipv6a(msg.getparam(proto::peername::ipv6a));
    auto ipv6b(msg.getparam(proto::peername::ipv6b));
    if (!ipv6a || !ipv6b) return Nothing;
    struct sockaddr_in6 in6;
    memset(&in6, 0, sizeof(in6));
    in6.sin6_family = AF_INET6;
    in6.sin6_port = port.just();
    ((uint64_t *)&in6.sin6_addr)[0] = ipv6a.just();
    ((uint64_t *)&in6.sin6_addr)[1] = ipv6b.just();
    return peername((struct sockaddr *)&in6, sizeof(in6));
}

peername::port::port(const quickcheck &q) {
    do {
        p = (unsigned short)q;
    } while (p == 0); }

peername::port::port(int _p)
    : p(_p) {
    assert(_p > 0 && _p < 65536); }

peername
peername::udpbroadcast(peername::port p)
{
    struct sockaddr_in sin;
    assert(p.p > 0 && p.p < 65536);
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)p.p);
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
#ifndef COVERAGESKIP
        abort();
#endif
    } }

const struct sockaddr *
peername::sockaddr() const {
    return (const struct sockaddr *)sockaddr_; }

unsigned
peername::sockaddrsize() const {
    return sockaddrsize_; }

class _ip6litparser : public parser<struct in6_addr> {
private: orerror<result> parse(const char *what) const;
};
static _ip6litparser ip6litparser;
orerror<_ip6litparser::result>
_ip6litparser::parse(const char *what) const {
    if (what[0] != '[') return error::noparse;
    const char *end = strchr(what, ']');
    if (!end) return error::noparse;
    char buf[end - what];
    memcpy(buf, what + 1, end - what - 1);
    buf[end - what - 1] = '\0';
    struct in6_addr res;
    if (inet_pton(AF_INET6, buf, &res) != 1) return error::noparse;
    else return result(res, end + 1); }

const parser<peername> &
parsers::_peername() {
    auto parseunix([] () -> const parser<peername> & {
        return ("unix://" + strparser + "/")
            .maperr<peername>(
                [] (const orerror<const char *> &what) -> orerror<peername> {
                    struct sockaddr_un sun;
                    if (what.isfailure()) {
                        return what.failure(); }
                    else if (strlen(what.success()) >= sizeof(sun.sun_path)) {
                        return error::overflowed; }
                    else {
                        memset(&sun, 0, sizeof(sun));
                        sun.sun_family = AF_UNIX;
                        strcpy(sun.sun_path, what.success());
                        return peername((const sockaddr *)&sun,
                                        sizeof(sun)); } })
            || (strmatcher("unix*:///")
                .map<peername>([] {
                        sa_family_t s = AF_UNIX;
                        return peername((const struct sockaddr *)&s,
                                        sizeof(s)); })); });
    auto parseip([] () -> const parser<peername> & {
        return ("ip://" +
                ~(intparser<unsigned>() + "." +
                  intparser<unsigned>() + "." +
                  intparser<unsigned>() + "." +
                  intparser<unsigned>()) +
                ~(":" + intparser<unsigned>()) + "/")
            .maperr<peername>(
                [] (const orerror<pair<maybe<pair<pair<pair<unsigned,
                                                            unsigned>,
                                                       unsigned>,
                                                  unsigned> >,
                                       maybe<unsigned> > > &_x)
                    -> orerror<peername> {
                    if (_x.isfailure()) return _x.failure();
                    auto &x(_x.success());
                    struct sockaddr_in sin;
                    memset(&sin, 0, sizeof(sin));
                    sin.sin_family = AF_INET;
                    if (x.first().isjust()) {
                        auto p(x.first().just());
                        auto a(p.first().first().first());
                        auto b(p.first().first().second());
                        auto c(p.first().second());
                        auto d(p.second());
                        if (a > 255 || b > 255 || c > 255 || d > 255) {
                            return error::noparse; };
                        sin.sin_addr.s_addr =
                            (a << 24) |
                            (b << 16) |
                            (c << 8) |
                            d; }
                    if (x.second().isjust()) {
                        if (x.second().just() >= 65536) return error::noparse;
                        sin.sin_port = htons((uint16_t)x.second().just()); }
                    return peername((const struct sockaddr *)&sin,
                                    sizeof(sin)); }); });
    auto parseip6([] () -> const parser<peername> & {
            return ("ip6://" + ~ip6litparser +
                    ~(":" + intparser<unsigned>()) + "/")
                .maperr<peername>(
                    [] (const orerror<pair<maybe<in6_addr>,
                                           maybe<unsigned> > > &_x)
                    -> orerror<peername> {
                        if (_x.isfailure()) return _x.failure();
                        auto &x(_x.success());
                        struct sockaddr_in6 sin6;
                        memset(&sin6, 0, sizeof(sin6));
                        sin6.sin6_family = AF_INET6;
                        if (x.first().isjust()) {
                            sin6.sin6_addr = x.first().just(); }
                        if (x.second().isjust()) {
                            if (x.second().just() >= 65536) {
                                return error::noparse; }
                            sin6.sin6_port = htons(
                                (uint16_t)x.second().just()); }
                        return peername((const struct sockaddr *)&sin6,
                                        sizeof(sin6)); }); });
    return parseunix() || parseip() || parseip6(); }

const parser<peername::port> &
parsers::_peernameport() {
    return ("<port:" + intparser<unsigned short>() + ">")
        .map<peername::port>([] (const unsigned short &x) {
                return peername::port(x); }); }

void
tests::_peername() {
    testcaseV("peername", "parser",
              [] { parsers::roundtrip(parsers::_peername()); });
    testcaseV("peername", "status", [] {
            peername p((quickcheck()));
            assert(p.status() == p); });
    testcaseV("peername", "wireproto", [] {
            wireproto::roundtrip<peername>();});
    testcaseV("peername", "fieldbad", [] {
            int x = 93;
            peername p((const sockaddr *)&x, sizeof(x));
            fields::print(fields::mk(p) + "\n"); });
    testcaseV("peername", "udpbroadcast", [] {
            auto p(peername::udpbroadcast(peername::port(97)));
            auto sin = (const struct sockaddr_in *)p.sockaddr();
            assert(p.sockaddrsize() == sizeof(*sin));
            assert(sin->sin_port == htons(97));
            assert(sin->sin_addr.s_addr == 0xffffffff); });
    testcaseV("peername", "tcpany", [] {
            auto p(peername::tcpany());
            auto sin = (const struct sockaddr_in *)p.sockaddr();
            assert(p.sockaddrsize() == sizeof(*sin));
            assert(sin->sin_port == 0);
            assert(sin->sin_addr.s_addr == 0); });
    testcaseV("peername", "samehost", [] {
            for (unsigned x = 0; x < 100; x++) {
                peername p1((quickcheck()));
                assert(p1.samehost(p1)); }});
    testcaseV("peername", "parseedge", [] {
            assert(parsers::_peername()
                   .match("unix://\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\"/")
                   == error::noparse);
            assert(parsers::_peername()
                   .match("ip://99999.1.2.3/")
                   == error::noparse);
            assert(parsers::_peername()
                   .match("ip6://[::]:99999/")
                   == error::noparse); });
    testcaseV("peername", "peernameport", [] {
            parsers::roundtrip(parsers::_peernameport()); }); }
