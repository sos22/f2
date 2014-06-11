#include "udpsocket.H"

#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

#include "buffer.H"
#include "orerror.H"
#include "peername.H"
#include "timedelta.H"

orerror<udpsocket>
udpsocket::listen(port p) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return error::from_errno();
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(p.p);
    if (bind(fd, (const struct sockaddr *)&sin, sizeof(sin))) {
        ::close(fd);
        return error::from_errno(); }
    int mtu = IP_PMTUDISC_DONT;
    if (setsockopt(fd, SOL_IP, IP_MTU_DISCOVER, &mtu, sizeof(mtu)) < 0) {
        ::close(fd);
        return error::from_errno(); }
    
    return udpsocket(fd); }

struct pollfd
udpsocket::poll() const {
    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    return pfd; }

void
udpsocket::close() const {
    ::close(fd); }

orerror<peername>
udpsocket::receive(buffer &buf, maybe<timestamp> deadline) const {
    ssize_t recved;
    unsigned char sockaddr[4096];
    socklen_t sockaddr_size;
    char rxbuf[65536];
    
    sockaddr_size = sizeof(sockaddr);
    if (deadline == Nothing) {
        recved = recvfrom(fd, rxbuf, sizeof(rxbuf), 0,
                          (struct sockaddr *)sockaddr, &sockaddr_size);
    } else {
        auto pfd(poll());
        while (1) {
            auto remaining(deadline.just() - timestamp::now());
            if (remaining < timedelta::seconds(0))
                return error::timeout;
            auto r(::poll(&pfd, 1, remaining.as_milliseconds()));
            if (r < 0) return error::from_errno();
            if (r == 1) break;
            assert(r == 0);
        }
        recved = recvfrom(fd, rxbuf, sizeof(rxbuf), MSG_DONTWAIT,
                          (struct sockaddr *)sockaddr, &sockaddr_size); }
    if (recved < 0) return error::from_errno();
    buf.queue(rxbuf, recved);
    return peername((const struct sockaddr *)sockaddr,
                    sockaddr_size); }
