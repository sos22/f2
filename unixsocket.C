#include "unixsocket.H"

#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <unistd.h>

orerror<listenfd>
unixsocket::listen(const char *path)
{
    error err;

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
	err = error::from_errno();
	goto failed;
    }
    struct sockaddr_un sa;
    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, "mastersock");
    if (bind(sock, (const struct sockaddr *)&sa, sizeof(sa)) < 0) {
	if (errno == EADDRINUSE) {
	    if (unlink(sa.sun_path) < 0 ||
		bind(sock, (const struct sockaddr *)&sa, sizeof(sa)) < 0) {
		err = error::from_errno();
		goto failed;
	    }
	} else {
	    err = error::from_errno();
	    goto failed;
	}
    }
    if (::listen(sock, 5) < 0) {
	err = error::from_errno();
	goto failed;
    }
    return orerror<listenfd>::success(listenfd(sock));
failed:
    ::close(sock);
    return orerror<listenfd>::failure(err);
}

orerror<fd_t>
unixsocket::connect(const char *path)
{
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0)
	return orerror<fd_t>::failure(error::from_errno());
    struct sockaddr_un sa;
    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, path);
    if (::connect(sock, (const struct sockaddr *)&sa, sizeof(sa)) < 0) {
	::close(sock);
	return orerror<fd_t>::failure(error::from_errno());
    }
    return orerror<fd_t>::success(fd_t(sock));
}

