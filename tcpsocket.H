#ifndef TCPSOCKET_H__
#define TCPSOCKET_H__

class clientio;
template <typename> class orerror;
class peername;
class socket_t;

class tcpsocket {
public:
    static orerror<socket_t> connect(clientio, const peername &p);
};

#endif /* !TCPSOCKET_H__ */
