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
    maybe<error> send(const wireproto::tx_message &msg,
		      wireproto::sequencenr snr);
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
	return r.failure();
    return (::controlclient *)new controlclient(r.success());
}

maybe<error>
controlclient::send(const wireproto::tx_message &msg,
		    wireproto::sequencenr seq)
{
    {
	auto r(msg.serialise(outgoing, seq));
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
controlclient::call(const wireproto::tx_message &msg)
{
    auto snr(sequencer.get());
    auto r = send(msg, snr);
    if (r.isjust()) {
	sequencer.put(snr);
	return r.just();
    }
    while (1) {
	auto m = receive();
	if (m.isfailure()) {
	    /* XXX leak the sequence number here.  Probably not a
	     * problem: the connection is almost certainly dead,
	     * anyway. */
	    return m;
	}
	if (m.success()->sequence == snr) {
	    sequencer.put(snr);
	    return m;
	}
	pendingrx.pushhead(m.success());
    }
}

orerror<const wireproto::rx_message *>
controlclient::receive()
{
    if (!pendingrx.empty()) {
	return pendingrx.pophead();
    }
    while (1) {
	auto r(wireproto::rx_message::fetch(incoming));
	if (r.isjust())
	    return r.just();
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
    return ((cc::controlclient *)this)->send(msg,
					     wireproto::sequencenr::invalid);
}

orerror<const wireproto::rx_message *>
controlclient::call(const wireproto::tx_message &msg)
{
    return ((cc::controlclient *)this)->call(msg);
}

#include "list.tmpl"
template class list<const wireproto::rx_message *>;
