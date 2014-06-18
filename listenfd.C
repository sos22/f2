#include "listenfd.H"

#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <stddef.h>
#include <unistd.h>

#include "fields.H"
#include "logging.H"
#include "peername.H"
#include "socket.H"

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
    
    /* Little tiny bit of a hack: if we only have the 0.0.0.0 address,
       find some plausible-looking interface and return that
       instead. */
    struct sockaddr_in *sin = (struct sockaddr_in *)addr;
    if (sin->sin_family == AF_INET &&
        sin->sin_addr.s_addr == 0) {
        int s = ::socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) error::from_errno().fatal("opening name query socket");
        struct ifreq reqs[128];
        struct ifconf arg;
        arg.ifc_len = sizeof(reqs);
        arg.ifc_req = reqs;
        if (ioctl(s, SIOCGIFCONF, &arg) < 0) {
            error::from_errno().fatal("getting interface list"); }
        bool found;
        found = false;
        for (unsigned x = 0;
             !found && x < arg.ifc_len / sizeof(arg.ifc_req[0]);
             x++) {
            if (reqs[x].ifr_addr.sa_family != AF_INET) continue;
            const struct sockaddr_in *candidate =
                (const struct sockaddr_in *)&reqs[x].ifr_addr;
            if (candidate->sin_addr.s_addr == 0) continue;
            struct ifreq req;
            memcpy(req.ifr_name, reqs[x].ifr_name, sizeof(req.ifr_name));
            if (ioctl(s, SIOCGIFFLAGS, &req) < 0) {
                error::from_errno().fatal("getting flags for interface "+
                                          fields::mk(reqs[x].ifr_name)); }
            if (!(req.ifr_flags & IFF_UP)) continue;
            if (!(req.ifr_flags & IFF_RUNNING)) continue;
            if (req.ifr_flags & IFF_LOOPBACK) continue;
            if (req.ifr_flags & IFF_POINTOPOINT) continue;
            logmsg(loglevel::debug,
                   "using " +
                   fields::mk(candidate->sin_addr.s_addr).base(16).nosep() +
                   " for anonymous socket");
            sin->sin_addr = candidate->sin_addr;
            found = true;
        }
        if (!found) {
            logmsg(loglevel::emergency,
                   fields::mk("cannot find any usable IP interfaces?"));
            _exit(1);
        }
        ::close(s);
    }
    return peername((const struct sockaddr *)addr, addrlen); }

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
