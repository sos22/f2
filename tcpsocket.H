#ifndef TCPSOCKET_H__
#define TCPSOCKET_H__

template <typename> class orerror;
class peername;
class socket_t;

class tcpsocket {
public:
    static orerror<socket_t> connect(const peername &p);
};

#endif /* !TCPSOCKET_H__ */
