#include "controlclient.H"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "unixsocket.H"
#include "wireproto.H"

namespace cc {
struct controlclient {
    buffer outgoing;
    fd_t fd;
    controlclient(fd_t _fd)
	: fd(_fd)
	{}
    ~controlclient()
	{
	    fd.close();
	}
    maybe<error> send(const wireproto::tx_message &msg);
};

static orerror< ::controlclient *>
connect()
{
    auto r(unixsocket::connect("mastersock"));
    if (r.isfailure())
	return orerror< ::controlclient *>::failure(r.failure());
    return orerror< ::controlclient *>::success(
	(::controlclient *)new controlclient(r.success()));
}

maybe<error>
controlclient::send(const wireproto::tx_message &msg)
{
    {
	auto r(msg.serialise(outgoing));
	if (r.isjust())
	    return r;
    }

    while (!outgoing.empty()) {
	auto r(outgoing.send(fd));
	if (r.isjust())
	    return r;
    }
    return maybe<error>::mknothing();
}

};

orerror<controlclient *>
controlclient::connect()
{
    return cc::connect();
}

void
controlclient::destroy() const
{
    delete (cc::controlclient *)this;
}

maybe<error>
controlclient::send(const wireproto::tx_message &msg)
{
    return ((cc::controlclient *)this)->send(msg);
}
