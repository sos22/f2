/* Simple thing for representing the name of the person on the other
 * end of a socket. */
#ifndef PEERNAME_H__
#define PEERNAME_H__

#include "wireproto.H"

struct sockaddr;

class peername {
    void *sockaddr_;
    unsigned sockaddrsize_;
    peername() = delete;
public:
    WIREPROTO_TYPE(peername);
    peername(const peername &);
    void operator=(const peername &);
    ~peername();
    peername(const struct sockaddr *s, unsigned s_size);
    bool operator==(const peername &o) const;

    /* A representation of the port part of an address.  Only really
       meaningful for IP and IP6 addresses; UNIX sockets return a
       fixed dummy value. */
    class port {
        friend class peername;
        friend class udpsocket;
        int p;
    public:
        explicit port(int);
        static const port unixport;
    };

    /* Get a peername which can be used to send a UDP broadcast on a
       particular port. */
    static peername udpbroadcast(port p);

    /* Get the peername which we should listen on if we want the OS to
       allocate us a port number. */
    static peername tcpany();

    /* Get a peername for a local UNIX domain socket. */
    static peername local(const char *path);

    /* Check whether two peernames refer to the same address and
       address family. */
    bool samehost(const peername &p) const;

    /* Get the sockaddr for this peer */
    const struct sockaddr *sockaddr() const;
    /* And the sockaddr size */
    unsigned sockaddrsize() const;

    /* peernames are their own status representation */
public: typedef peername status_t;
public: status_t status() const { return *this; }

    /* Parse a peername from a string */
public: static orerror<peername> parse(const char *);
};

namespace fields {
class field;
const field &mk(const peername &p);
};

#endif /* !PEERNAME_H__ */
