#include "peername.H"

#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#include "error.H"
#include "fields.H"

peername::peername(const peername &o)
    : sockaddr(malloc(o.sockaddrsize)),
      sockaddrsize(o.sockaddrsize)
{
    memcpy(sockaddr, o.sockaddr, o.sockaddrsize);
}

peername::peername(const struct sockaddr *s, size_t size)
    : sockaddr(malloc(size)),
      sockaddrsize(size)
{
    memcpy(sockaddr, s, size);
    switch ( ((struct sockaddr *)sockaddr)->sa_family) {
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
    free(sockaddr);
}

class peernamefield : fields::field {
    const peername &p;
    void fmt(fields::fieldbuf &) const;
    peernamefield(const peername &_p)
        : p(_p)
        {}
public:
    static const field &n(const peername &o)
        { return *new peernamefield(o); }
};

void
peernamefield::fmt(fields::fieldbuf &o) const
{
    int family = ((const struct sockaddr *)p.sockaddr)->sa_family;
    switch (family) {
    case AF_UNIX:
        o.push("unix://");
        o.push( ((const struct sockaddr_un *)p.sockaddr)->sun_path);
        o.push("/");
        break;
    case AF_INET: {
        auto addr((const struct sockaddr_in *)p.sockaddr);
        o.push("ip://");
        char buf[INET_ADDRSTRLEN];
        auto r(inet_ntop(family, &addr->sin_addr, buf, sizeof(buf)));
        if (r) {
            o.push(buf);
        } else {
            ("<error " +
             fields::mk(error::from_errno()) +
             " formatting peer address>").fmt(o);
        }
        o.push(":");
        fields::mk(htons(addr->sin_port)).fmt(o);
        o.push("/");
        break;
    }
    case AF_INET6: {
        auto addr((const struct sockaddr_in6 *)p.sockaddr);
        o.push("ip6://");
        char buf[INET6_ADDRSTRLEN];
        auto r(inet_ntop(family, &addr->sin6_addr, buf, sizeof(buf)));
        if (r) {
            o.push(buf);
        } else {
            ("<error " +
             fields::mk(error::from_errno()) +
             " formatting peer address>").fmt(o);
        }
        o.push(":");
        fields::mk(htons(addr->sin6_port)).fmt(o);
        o.push("/");
        break;
    }
    }
}

const fields::field &
fields::mk(const peername &p)
{
    return peernamefield::n(p);
}
