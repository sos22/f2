#include "unixsocket.H"

#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <unistd.h>

orerror<fd_t>
unixsocket::connect(const char *path)
{
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0)
        return error::from_errno();
    struct sockaddr_un sa;
    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, path);
    if (::connect(sock, (const struct sockaddr *)&sa, sizeof(sa)) < 0) {
        ::close(sock);
        return error::from_errno();
    }
    return fd_t(sock);
}
