/* Simple thing for representing the name of the person on the other
 * end of a socket. */
/* XXX this is a horrible mess.  Should treat UNIX-domain peernames
 * differently from IP ones. */
#ifndef PEERNAME_H__
#define PEERNAME_H__

class deserialise1;
namespace fields { class field; }
class filename;
template <typename> class orerror;
class peername;
class quickcheck;
class serialise1;
struct sockaddr;

/* A representation of the port part of an address.  Only really
   meaningful for IP and IP6 addresses; UNIX sockets return a fixed
   dummy value. */
class peernameport {
    friend class peername;
    friend class udpsocket;
private: unsigned short p; /* host byte order */
public:  peernameport(const quickcheck &q);
public:  explicit peernameport(unsigned short _p) : p(_p) {}
public:  explicit peernameport(deserialise1 &);
public:  void serialise(serialise1 &) const;
public:  bool operator==(const peernameport &o) const { return p == o.p; }
public:  bool operator!=(const peernameport &o) const { return p != o.p; }
public:  const fields::field &field() const;
    /* Special port which can be used to get the OS to allocate an
       arbitrary port for you. */
    const static peernameport any;
};

class peername {
    void *sockaddr_;
    unsigned sockaddrsize_;
    peername() = delete;
public:
    peername(const quickcheck &);
    peername(const peername &);
    explicit peername(deserialise1 &);
    void serialise(serialise1 &) const;
    void operator=(const peername &);
    ~peername();
    peername(const struct sockaddr *s, unsigned s_size);
    bool operator==(const peername &o) const;

    typedef peernameport port;

    /* Get a peername which can be used to send a UDP broadcast on a
       particular port. */
    static peername udpbroadcast(port p);

    /* Special peername used to listen on all interfaces on a
     * particular port. */
    static peername all(port);

    /* Loopback IP address */
    static peername loopback(port);

    /* Check whether two peernames refer to the same address and
       address family. */
    bool samehost(const peername &p) const;

    /* Check whether a given address is a broadcast address (or, more
       accurately, any non-unicast address) */
    bool isbroadcast() const;

    /* Construct a new peername which refers to the same host but on a
     * different port. */
    peername setport(port) const;

    /* Extract the port part of the peername. */
    port getport() const;

    /* Get the sockaddr for this peer */
    const struct sockaddr *sockaddr() const;
    /* And the sockaddr size */
    unsigned sockaddrsize() const;

    /* peernames are their own status representation */
public: typedef peername status_t;
public: status_t status() const { return *this; }

public: const fields::field &field() const;
};

namespace fields {
class field;
const field &mk(const peername &p);
};

template <typename> class parser;
namespace parsers {
const parser<peername> &_peername();
const parser<peername::port> &_peernameport();
}

namespace tests {
void _peername();
}

#endif /* !PEERNAME_H__ */
