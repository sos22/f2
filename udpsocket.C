#include "udpsocket.H"

#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

#include "buffer.H"
#include "fd.H"
#include "fields.H"
#include "logging.H"
#include "peername.H"
#include "timedelta.H"

#include "orerror.tmpl"
#include "test.tmpl"

tests::hookpoint<orerror<udpsocket>, udpsocket >
udpsocket::_client(
    [] (udpsocket c) -> orerror<udpsocket> { return c; });

tests::hookpoint<orerror<void>, const udpsocket &>
udpsocket::_receive([] (const udpsocket&) -> orerror<void> { return Success; });

tests::hookpoint<orerror<void>, const udpsocket &, const peername &>
udpsocket::_send([] (const udpsocket &, const peername &) { return Success; });

orerror<udpsocket>
udpsocket::listen(peername::port p) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return error::from_errno();
    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
        ::close(fd);
        return error::from_errno(); }
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    assert(p.p > 0);
    sin.sin_port = htons(p.p);
    if (bind(fd, (const struct sockaddr *)&sin, sizeof(sin))) {
        ::close(fd);
        return error::from_errno();
    }
    int mtu = IP_PMTUDISC_DONT;
    if (setsockopt(fd, SOL_IP, IP_MTU_DISCOVER, &mtu, sizeof(mtu)) < 0) {
        ::close(fd);
        return error::from_errno();
    }

    return udpsocket(fd);
}

orerror<udpsocket>
udpsocket::client()
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return error::from_errno();
    int allow_broadcast = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &allow_broadcast,
                   sizeof(allow_broadcast)) < 0) {
        ::close(fd);
        return error::from_errno();
    }
    int mtu = IP_PMTUDISC_DONT;
    if (setsockopt(fd, SOL_IP, IP_MTU_DISCOVER, &mtu, sizeof(mtu)) < 0) {
        ::close(fd);
        return error::from_errno(); }
    
    return _client(udpsocket(fd)); }

struct pollfd
udpsocket::poll() const {
    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    pfd.events = POLLIN | POLLERR | POLLHUP | POLLNVAL;
    pfd.revents = 0;
    return pfd; }

fd_t
udpsocket::asfd() const {
    return fd_t(fd); }

void
udpsocket::close() const {
    ::close(fd); }

orerror<peername>
udpsocket::receive(clientio, buffer &buf, maybe<timestamp> deadline) const {
    auto inject(_receive(*this));
    if (inject.isfailure()) return inject.failure();

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
            auto remaining(
                (deadline.just() - timestamp::now()).as_milliseconds());
            if (remaining < 0)
                return error::timeout;
            assert(remaining == (int)remaining);
            auto r(::poll(&pfd, 1, (int)remaining));
            if (r < 0) return error::from_errno();
            if (r == 1) break;
            assert(r == 0);
        }
        recved = recvfrom(fd, rxbuf, sizeof(rxbuf), MSG_DONTWAIT,
                          (struct sockaddr *)sockaddr, &sockaddr_size); }
    if (recved < 0) return error::from_errno();
    buf.queue(rxbuf, (size_t)recved);
    peername res((const struct sockaddr *)sockaddr,
                 sockaddr_size);
    if (res.isbroadcast()) {
        logmsg(loglevel::failure,
               "Received udp packet from a broadcast address " +
               fields::mk(res) +
               ", dropping");
        return error::invalidmessage; }
    return res; }

orerror<void>
udpsocket::send(buffer &buf, const peername &p) const {
    auto fault(_send(*this, p));
    if (fault.isfailure()) return fault.failure();
    size_t bytes(buf.avail());
    ssize_t sent(::sendto(fd,
                          buf.linearise(buf.offset(), buf.offset() + bytes),
                          bytes,
                          MSG_NOSIGNAL,
                          p.sockaddr(),
                          p.sockaddrsize()));
    if (sent >= 0) {
        buf.discard(bytes);
        if ((size_t)sent != bytes)
            logmsg(loglevel::failure,
                   "tried to send " + fields::mk(bytes) +
                   " UDP packet, truncated to " + fields::mk(sent));
        return Success;
    } else {
        return error::from_errno(); } }

const fields::field &
fields::mk(udpsocket s) { return mk(fd_t(s.fd)); }
