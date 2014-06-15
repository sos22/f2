#ifndef UNIXSOCKET_H__
#define UNIXSOCKET_H__

#include "fd.H"
#include "listenfd.H"
#include "orerror.H"

/* Use a class rather than a namespace so that we can be friends with
   listenfd and fd_t */
class unixsocket {
public:
    static orerror<fd_t> connect(const char *path);
};

#endif /* !UNIXSOCKET_H__ */
