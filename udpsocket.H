#ifndef UDPSOCKET_H__
#define UDPSOCKET_H__

#include "maybe.H"
#include "timestamp.H"

class buffer;
class error;
template <typename> class orerror;
class peername;

class udpsocket {
    int fd;
    udpsocket(int _fd) : fd(_fd) {}
public:
    udpsocket() : fd(-1) {}
    class port {
        friend class udpsocket;
        int p;
    public:
        port(int _p) : p(_p) {}
    };

    /* UDP sockets don't really listen, as such.  This creates a
       socket bound to a particular port (on 0.0.0.0) which receives
       packets sent on that port. */
    static orerror<udpsocket> listen(port);

    /* Create a client UDP socket.  This can send to anywhere with
       send() and receive from anywhere on an OS-allocated port.
       The port can be retrieved with localname(). */
    static orerror<udpsocket> client();

    /* Return a peername on which this socket can receive packets. */
    peername localname() const;

    /* Receive a packet and add it to a buffer, returning the peername
       from which we received it (according to the UDP envelope, so
       easily faked by an attacker). */
    orerror<peername> receive(buffer &buf,
                              maybe<timestamp> deadline = Nothing) const;

    /* Send a packet to a particular peer. */
    maybe<error> send(buffer &buf, const peername &p) const;

    /* Return a pollfd for checking whether this FD is currently
     * readable. */
    struct pollfd poll() const;

    /* Close the underlying FD. */
    void close() const;
};

#endif /* !UDPSOCKET_H__ */
