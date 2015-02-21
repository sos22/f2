#include "peername.H"

#include <sys/ioctl.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <unistd.h>

#include "either.H"
#include "error.H"
#include "fields.H"
#include "listenfd.H"
#include "logging.H"
#include "parsers.H"
#include "quickcheck.H"
#include "serialise.H"
#include "socket.H"
#include "spark.H"
#include "tcpsocket.H"
#include "test.H"
#include "util.H"

#include "parsers.tmpl"
#include "serialise.tmpl"
#include "spark.tmpl"

#include "fieldfinal.H"

const peername::port
peername::port::any(0);

peernameport::peernameport(deserialise1 &ds)
    : p(ds) {}

void
peernameport::serialise(serialise1 &s) const {
    s.push(p); }

peername::peername(const quickcheck &q) {
    switch (random() % 2) {
    case 0: {
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
    case 1: {
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
        break; }
    default: abort(); } }

peername::peername(const peername &o)
    : sockaddr_(malloc(o.sockaddrsize_ + 1)),
      sockaddrsize_(o.sockaddrsize_)
{
    ((char *)sockaddr_)[sockaddrsize_] = 0;
    memcpy(sockaddr_, o.sockaddr_, o.sockaddrsize_);
}

peername::peername(deserialise1 &ds)
    : sockaddr_(NULL),
      sockaddrsize_(0) {
    if (ds.random()) {
        new (this) peername(quickcheck());
        return; }
    sockaddrsize_ = ds;
    /* Arbitrary limit, for general sanity. */
    if (sockaddrsize_ > 500) {
        ds.fail(error::overflowed);
        sockaddrsize_ = 500; }
    sockaddr_ = malloc(sockaddrsize_);
    ds.bytes(sockaddr_, sockaddrsize_);
    auto sa((struct sockaddr *)sockaddr_);
    if (sa->sa_family == AF_INET) {
        if (sockaddrsize_ != sizeof(struct sockaddr_in)) {
            ds.fail(error::invalidmessage); } }
    else if (sa->sa_family == AF_INET6) {
        if (sockaddrsize_ != sizeof(struct sockaddr_in6)) {
            ds.fail(error::invalidmessage); } }
    else ds.fail(error::invalidmessage);
    if (ds.isfailure()) {
        free(sockaddr_);
        sockaddrsize_ = sizeof(struct sockaddr_in);
        sockaddr_ = malloc(sockaddrsize_);
        auto sin((struct sockaddr_in *)sockaddr_);
        memset(sin, 0, sizeof(*sin));
        sin->sin_family = AF_INET;
        sin->sin_port = 12345;
        sin->sin_addr.s_addr = htonl(INADDR_LOOPBACK); } }

void
peername::serialise(serialise1 &s) const {
    /* Match deserialiser limit. */
    assert(sockaddrsize_ <= 500);
    s.push(sockaddrsize_);
    s.bytes(sockaddr_, sockaddrsize_); }

void
peername::operator=(const peername &o) {
    free(sockaddr_);
    sockaddr_ = malloc(o.sockaddrsize_ + 1);
    sockaddrsize_ = o.sockaddrsize_;
    memcpy(sockaddr_, o.sockaddr_, sockaddrsize_ + 1); }

peername::peername(const struct sockaddr *s, unsigned size)
    : sockaddr_(malloc(size + 1)),
      sockaddrsize_(size)
{
    ((char *)sockaddr_)[size] = 0;
    memcpy(sockaddr_, s, size);
    switch (s->sa_family) {
    case AF_INET:
        assert(size == sizeof(struct sockaddr_in));
        break;
    case AF_INET6:
        assert(size == sizeof(struct sockaddr_in6));
        break;
    default:
        abort();
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
    case AF_INET: {
        auto addr((const struct sockaddr_in *)sa);
        return "ip://" +
            fields::mk((addr->sin_addr.s_addr >> 0) & 0xff) + "." +
            fields::mk((addr->sin_addr.s_addr >> 8) & 0xff) + "." +
            fields::mk((addr->sin_addr.s_addr >> 16) & 0xff) + "." +
            fields::mk((addr->sin_addr.s_addr >> 24) & 0xff) + ":" +
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
        abort();
    }
}

const fields::field &
fields::mk(const peername::port &p) {
    return "<port:" + mk(p.p) + ">"; }

peername::port::peernameport(const quickcheck &q) {
    do {
        p = (unsigned short)q;
    } while (p <= 1024); }

peername
peername::udpbroadcast(peername::port p)
{
    struct sockaddr_in sin;
    assert(p.p != 0);
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(p.p);
    sin.sin_addr.s_addr = INADDR_BROADCAST;
    return peername((const struct sockaddr *)&sin, sizeof(sin));
}

peername
peername::all(port p)
{
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(p.p);
    return peername((const struct sockaddr *)&sin, sizeof(sin));
}

peername
peername::loopback(port p)
{
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = htons(p.p);
    return peername((const struct sockaddr *)&sin, sizeof(sin));
}

bool
peername::samehost(const peername &o) const {
    auto us(sockaddr());
    auto them(o.sockaddr());
    if (us->sa_family != them->sa_family) return false;
    switch (us->sa_family) {
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
        abort();
    } }

/* Check whether the address represented is a non-unicast one. */
bool
peername::isbroadcast() const {
    switch (sockaddr()->sa_family) {
    case PF_INET: {
        auto sa((const struct sockaddr_in *)sockaddr());
        return (sa->sin_addr.s_addr & 0xf0) == 0xe0 ||
            sa->sin_addr.s_addr == 0 ||
            sa->sin_addr.s_addr == 0xffffffff; }
    case PF_INET6: {
        auto sa((const struct sockaddr_in6 *)sockaddr());
        return sa->sin6_addr.s6_addr[0] == 0xff; }
    default:
        abort(); } }

peername
peername::setport(port p) const {
    switch (sockaddr()->sa_family) {
    case PF_INET: {
        auto sa(*(const struct sockaddr_in *)sockaddr());
        sa.sin_port = htons(p.p);
        return peername((const struct sockaddr *)&sa, sizeof(sa)); }
    case PF_INET6: {
        auto sa(*(const struct sockaddr_in6 *)sockaddr());
        sa.sin6_port = htons(p.p);
        return peername((const struct sockaddr *)&sa, sizeof(sa)); }
    default: abort(); } }

peername::port
peername::getport() const {
    switch (sockaddr()->sa_family) {
    case PF_INET: {
        auto sa((const struct sockaddr_in *)sockaddr());
        return port(ntohs(sa->sin_port)); }
    case PF_INET6: {
        auto sa((const struct sockaddr_in6 *)sockaddr());
        return port(ntohs(sa->sin6_port)); }
    default: abort(); } }

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
    memcpy(buf, what + 1, (size_t)(end - what - 1));
    buf[end - what - 1] = '\0';
    struct in6_addr res;
    if (inet_pton(AF_INET6, buf, &res) != 1) return error::noparse;
    else return result(res, end + 1); }

const parser<peername> &
parsers::_peername() {
    auto parseip([] () -> const parser<peername> & {
        return ("ip://" +
                ~(intparser<unsigned>() + "." +
                  intparser<unsigned>() + "." +
                  intparser<unsigned>() + "." +
                  intparser<unsigned>()) +
                ~(":" + intparser<unsigned>()) + "/")
            .maperr<peername>(
                [] (const pair<maybe<pair<pair<pair<unsigned,
                                                    unsigned>,
                                               unsigned>,
                                          unsigned> >,
                               maybe<unsigned> > &x)
                -> orerror<peername> {
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
                            (d << 24) |
                            (c << 16) |
                            (b << 8) |
                            a; }
                    if (x.second().isjust()) {
                        if (x.second().just() >= 65536) return error::noparse;
                        sin.sin_port = htons((uint16_t)x.second().just()); }
                    return peername((const struct sockaddr *)&sin,
                                    sizeof(sin)); }); });
    auto parseip6([] () -> const parser<peername> & {
            return ("ip6://" + ~ip6litparser +
                    ~(":" + intparser<unsigned>()) + "/")
                .maperr<peername>(
                    [] (const pair<maybe<in6_addr>,
                                   maybe<unsigned> > &x)
                    -> orerror<peername> {
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
    return parseip() || parseip6(); }

const parser<peername::port> &
parsers::_peernameport() {
    return ("<port:" + intparser<unsigned short>() + ">")
        .map<peername::port>([] (const unsigned short x) {
                return peername::port(x); }); }

void
tests::_peername() {
    testcaseV("peername", "parser",
              [] { parsers::roundtrip(parsers::_peername()); });
    testcaseV("peername", "status", [] {
            peername p((quickcheck()));
            assert(p.status() == p); });
    testcaseV("peername", "udpbroadcast", [] {
            auto p(peername::udpbroadcast(peername::port(97)));
            auto sin = (const struct sockaddr_in *)p.sockaddr();
            assert(p.sockaddrsize() == sizeof(*sin));
            assert(sin->sin_port == htons(97));
            assert(sin->sin_addr.s_addr == 0xffffffff); });
    testcaseV("peername", "all", [] {
            auto p(peername::all(peername::port::any));
            auto sin = (const struct sockaddr_in *)p.sockaddr();
            assert(p.sockaddrsize() == sizeof(*sin));
            assert(sin->sin_port == 0);
            assert(sin->sin_addr.s_addr == 0); });
    testcaseV("peername", "loopback", [] {
            auto p(peername::loopback(peername::port::any));
            auto sin = (const struct sockaddr_in *)p.sockaddr();
            assert(p.sockaddrsize() == sizeof(*sin));
            assert(sin->sin_port == 0);
            assert(sin->sin_addr.s_addr == htonl(INADDR_LOOPBACK)); });
    testcaseV("peername", "samehost", [] {
            for (unsigned x = 0; x < 100; x++) {
                peername p1((quickcheck()));
                assert(p1.samehost(p1)); }});
    testcaseV("peername", "=", [] {
            peername p(peername::all(peername::port::any));
            peername q(peername::loopback(peername::port(73)));
            assert(!(p == q));
            p = q;
            assert(p == q); });
    testcaseV("peername", "parseedge", [] {
            assert(parsers::_peername()
                   .match("ip://99999.1.2.3/")
                   == error::noparse);
            assert(parsers::_peername()
                   .match("ip6://[::]:99999/")
                   == error::noparse); });
    testcaseV("peername", "peernameport", [] {
            parsers::roundtrip(parsers::_peernameport());
            quickcheck q;
            serialise<peername::port>(q);
            {   auto p(peername::loopback(peername::port(q)));
                assert(!p.isbroadcast());
                for (unsigned x = 0; x < 100; x++) {
                    peername::port prt(q);
                    assert(p.setport(prt).getport() == prt); } }
            {   struct sockaddr_in6 sin6;
                memset(&sin6, 0, sizeof(sin6));
                sin6.sin6_family = AF_INET6;
                peername p((struct sockaddr *)&sin6, sizeof(sin6));
                assert(!p.isbroadcast());
                for (unsigned x = 0; x < 100; x++) {
                    peername::port prt(q);
                    assert(p.setport(prt).getport() == prt); } }
        });
    testcaseV("peername", "serialise", [] {
            quickcheck q;
            serialise<peername>(q); });
    testcaseV("peername", "serialisebad", [] {
            {   ::buffer b;
                {   serialise1 s(b);
                    s.push((unsigned)10000); }
                {   deserialise1 ds(b);
                    peername ignore(ds);
                    assert(ds.failure() == error::overflowed); } }
            {   ::buffer b;
                {   serialise1 s(b);
                    s.push((unsigned)2);
                    s.push((unsigned short)AF_INET); }
                {   deserialise1 ds(b);
                    peername ignore(ds);
                    assert(ds.failure() == error::invalidmessage); } }
            {   ::buffer b;
                {   serialise1 s(b);
                    s.push((unsigned)2);
                    s.push((unsigned short)AF_INET6); }
                {   deserialise1 ds(b);
                    peername ignore(ds);
                    assert(ds.failure() == error::invalidmessage); } } }); }
