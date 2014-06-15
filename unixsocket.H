#ifndef UNIXSOCKET_H__
#define UNIXSOCKET_H__

#include "fd.H"
#include "listenfd.H"
#include "orerror.H"

/* Use a class rather than a namespace so that we can be friends with
   listenfd and fd_t */
class unixsocket {
public:
    /* Listening on a unix socket is a little different from listening
       on most other socket types, because if there's someone already
       there then we can just kick them out. */
    static orerror<listenfd> listen(const char *path);
    static orerror<fd_t> connect(const char *path);
};

#endif /* !UNIXSOCKET_H__ */
