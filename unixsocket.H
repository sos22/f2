#ifndef UNIXSOCKET_H__
#define UNIXSOCKET_H__

#include "listenfd.H"

class unixsocket {
public:
    static orerror<listenfd> listen(const char *path);
    static orerror<fd_t> connect(const char *path);
};

#endif /* !UNIXSOCKET_H__ */
