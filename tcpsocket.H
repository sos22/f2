#ifndef TCPSOCKET_H__
#define TCPSOCKET_H__

#include "fd.H"

template <typename> class orerror;
class peername;

class tcpsocket {
public:
    static orerror<fd_t> connect(const peername &p);
};

#endif /* !TCPSOCKET_H__ */
