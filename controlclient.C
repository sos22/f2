#include "controlclient.H"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "unixsocket.H"
#include "wireproto.H"

namespace cc {
struct controlclient {
    buffer outgoing;
    buffer incoming;
    fd_t fd;
    wireproto::sequencer sequencer;
    list<const wireproto::rx_message *> pendingrx;
    controlclient(fd_t _fd)
	: fd(_fd)
	{}
    ~controlclient()
	{
	    fd.close();
	}
    maybe<error> send(const wireproto::tx_message &msg);
    orerror<const wireproto::rx_message *> call(const wireproto::tx_message &msg);
    orerror<const wireproto::rx_message *> receive();
    wireproto::sequencenr allocsequencenr(void)
	{
	    return sequencer.get();
	}    
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

orerror<const wireproto::rx_message *>
controlclient::call(const wireproto::tx_message &msg)
{
    auto r = send(msg);
    if (r.isjust()) {
	return orerror<const wireproto::rx_message *>::failure(r.just());
    }
    while (1) {
	auto m = receive();
	if (m.isfailure())
	    return m;
	if (m.success()->sequence == msg.sequence)
	    return m;
	pendingrx.push(m.success());
    }
}

orerror<const wireproto::rx_message *>
controlclient::receive()
{
    while (1) {
	auto r(wireproto::rx_message::fetch(incoming));
	if (r.isjust())
	    return orerror<const wireproto::rx_message *>::success(r.just());
	auto t(incoming.receive(fd));
	if (t.isjust())
	    return orerror<const wireproto::rx_message *>::failure(t.just());
    }
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

wireproto::sequencenr
controlclient::allocsequencenr()
{
    return ((cc::controlclient *)this)->allocsequencenr();
}

orerror<const wireproto::rx_message *>
controlclient::call(const wireproto::tx_message &msg)
{
    return ((cc::controlclient *)this)->call(msg);
}

#include "list.tmpl"
template class list<const wireproto::rx_message *>;
