#ifndef UDPSOCKET_H__
#define UDPSOCKET_H__

#include "peername.H"
#include "test.H"
#include "timestamp.H"

class buffer;
class clientio;
class error;
class fd_t;
template <typename> class maybe;
template <typename> class orerror;

class udpsocket;

namespace fields {
class field;
const field &mk(udpsocket); }

class udpsocket {
    friend const fields::field &fields::mk(udpsocket);
    int fd;
    explicit udpsocket(int _fd) : fd(_fd) {}
public:
    udpsocket() : fd(-1) {}

    bool operator==(const udpsocket &o) const { return fd == o.fd; }
    bool operator!=(const udpsocket &o) const { return fd != o.fd; }

    /* UDP sockets don't really listen, as such.  This creates a
       socket bound to a particular port (on 0.0.0.0) which receives
       packets sent on that port. */
    static orerror<udpsocket> listen(peername::port);

    /* Create a client UDP socket.  This can send to anywhere with
       send() and receive from anywhere on an OS-allocated port.
       The port can be retrieved with localname(). */
    static orerror<udpsocket> client();

     /* Return a pollfd for checking whether this FD is currently
     * readable. */
    struct pollfd poll() const;

    /* Send a UDP packet to a particular host and port, which must be
     * either UDP or UDP6, using the source address associated with
     * this socket. */
    orerror<void> send(buffer &, const peername &) const;

    /* Receive a packet and add it to a buffer, returning the peername
       from which we received it (according to the UDP envelope, so
       easily faked by an attacker). */
    orerror<peername> receive(clientio,
                              buffer &buf,
                              maybe<timestamp> deadline = Nothing) const;

    /* Extract an fd_t for this socket.  Be careful with this: udp
       sockets don't behave quite the same as byte stream sockets, so
       not everything will work as you might expect. */
    fd_t asfd() const;

    /* Close the underlying FD. */
    void close() const;

    /* Fault injection hooks: */
    static tests::hookpoint<orerror<udpsocket>, udpsocket > _client;
    static tests::hookpoint<orerror<void>, const udpsocket &> _receive;
    static tests::hookpoint<orerror<void>,
                            const udpsocket &,
                            const peername &> _send;
};

#endif /* !UDPSOCKET_H__ */
