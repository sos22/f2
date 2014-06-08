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
    orerror<const wireproto::rx_message *> call(const wireproto::req_message &msg);
    orerror<const wireproto::rx_message *> receive();
    orerror<const wireproto::rx_message *> _receive();
    wireproto::sequencenr allocsequencenr(void)
	{
	    return sequencer.get();
	}
    void putsequencenr(wireproto::sequencenr snr)
	{
	    sequencer.put(snr);
	}
};

static orerror< ::controlclient *>
connect()
{
    auto r(unixsocket::connect("mastersock"));
    if (r.isfailure())
	return r.failure();
    return (::controlclient *)new controlclient(r.success());
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
    return Nothing;
}

orerror<const wireproto::rx_message *>
controlclient::call(const wireproto::req_message &msg)
{
    auto r = send(msg);
    if (r.isjust())
	return r.just();
    while (1) {
	auto m = _receive();
	if (m.isfailure())
	    return m;
	if (m.success()->sequence == msg.sequence.reply())
	    return m;
	pendingrx.pushhead(m.success());
    }
}

orerror<const wireproto::rx_message *>
controlclient::receive()
{
    if (!pendingrx.empty()) {
	return pendingrx.pophead();
    }
    return _receive();
}

orerror<const wireproto::rx_message *>
controlclient::_receive()
{
    while (1) {
	auto r(wireproto::rx_message::fetch(incoming));
	if (r.issuccess())
	    return r.success();
	if (r.isfailure() && r.failure() != error::underflowed)
	    return r.failure();
	auto t(incoming.receive(fd));
	if (t.isjust())
	    return t.just();
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

orerror<const wireproto::rx_message *>
controlclient::call(const wireproto::req_message &msg)
{
    return ((cc::controlclient *)this)->call(msg);
}

wireproto::sequencenr
controlclient::allocsequencenr()
{
    return ((cc::controlclient *)this)->allocsequencenr();
}

void
controlclient::putsequencenr(wireproto::sequencenr snr)
{
    return ((cc::controlclient *)this)->putsequencenr(snr);
}

#include "list.tmpl"
template class list<const wireproto::rx_message *>;
