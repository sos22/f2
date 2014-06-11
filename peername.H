/* Simple thing for representing the name of the person on the other
 * end of a socket. */
#ifndef PEERNAME_H__
#define PEERNAME_H__

#include "fields.H"

struct sockaddr;

class peername {
    friend class peernamefield;
    void *sockaddr;
    size_t sockaddrsize;
    peername() = delete;
    void operator=(const peername &) = delete;
public:
    peername(const peername &);
    ~peername();
    peername(const struct sockaddr *s, size_t s_size);
};

namespace fields {
class field;
const field &mk(const peername &p);
};

#endif /* !PEERNAME_H__ */
