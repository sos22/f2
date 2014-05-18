#include "wireproto.H"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "buffer.H"

#include "list.tmpl"

template class list<wireproto::tx_message::pinstance>;

namespace wireproto {

tx_message::tx_message(msgtag _t)
    : t(_t)
{}

tx_message::~tx_message()
{
    params.flush();
}

maybe<error>
tx_message::serialise(buffer &buffer) const
{
    uint16_t sz;
    uint16_t nrparams;
    nrparams = 0;
    sz = 6;
    for (auto it(params.start()); !it.finished(); it.next()) {
	sz += it->serialised_size() + 4;
	nrparams++;
    }
    if (sz > 0x8000)
	return maybe<error>::mkjust(error::overflowed);
    buffer.queue(&sz, sizeof(sz));
    buffer.queue(&nrparams, sizeof(nrparams));
    buffer.queue(&t.val, sizeof(t.val));
    uint16_t consumed = 0;
    for (auto it(params.start()); !it.finished(); it.next()) {
	buffer.queue(&it->id, sizeof(it->id));
	buffer.queue(&consumed, sizeof(consumed));
	consumed += it->serialised_size();
    }
    for (auto it(params.start()); !it.finished(); it.next())
	it->serialise(buffer);
    return maybe<error>::mknothing();
}

maybe<const rx_message *>
rx_message::fetch(buffer &buffer)
{
    uint16_t sz;
    uint16_t nrparams;
    uint16_t tag;
    if (buffer.avail() < sizeof(sz) + sizeof(nrparams))
	return maybe<const rx_message *>::mknothing();
    buffer.fetch(&sz, sizeof(sz));
    if (buffer.avail() + sizeof(sz) < sz) {
	buffer.pushback(&sz, sizeof(sz));
	return maybe<const rx_message *>::mknothing();
    }
    if (sz < sizeof(sz) + sizeof(nrparams) + sizeof(tag))
	return NULL;
    buffer.fetch(&nrparams, sizeof(nrparams));
    buffer.fetch(&tag, sizeof(tag));
    if (nrparams > 128 || sz < 6 + nrparams * 4)
	return NULL;
    auto work(new rx_message(msgtag(tag),
			     nrparams,
			     sz - 6 - nrparams * 4,
			     buffer.offset() + 4 * nrparams,
			     buffer));
    buffer.fetch(work->index, 4 * nrparams);
    for (unsigned i = 0; i < nrparams; i++) {
	if (work->index[i].offset > work->payload_size) {
	    work->finish();
	    return NULL;
	}
    }
    return work;
}

void
rx_message::finish() const
{
    buf.discard(payload_size);
    free(index);
    delete this;
}

rx_message::rx_message(msgtag _t, uint16_t _nrparams,
		       size_t _payload_size, size_t _payload_offset,
		       buffer &_buf)
    : index((idx *)calloc(4, _nrparams)),
      nrparams(_nrparams),
      payload_size(_payload_size),
      payload_offset(_payload_offset),
      buf(_buf),
      t(_t)
{}

template <> tx_message &
tx_message::addparam(parameter<const char *> tmpl, const char *val)
{
    auto p(params.append());
    p->id = tmpl.id;
    p->flavour = pinstance::p_string;
    p->string = val;
    return *this;
}

size_t
tx_message::pinstance::serialised_size() const
{
    switch (flavour) {
    case p_string:
	return strlen(string) + 1;
    }
    abort();
}

void
tx_message::pinstance::serialise(buffer &buf) const
{
    switch (flavour) {
    case p_string:
	buf.queue(string, strlen(string) + 1);
	return;
    }
    abort();
}

struct bufslice {
    buffer &buf;
    unsigned long start;
    unsigned long end;
    bufslice(buffer &_buf, unsigned long _start,
	     unsigned long _end)
	: buf(_buf), start(_start), end(_end)
	{}
};

template <typename typ> maybe<typ> deserialise(bufslice &slice);

template <> maybe<const char *>
deserialise(bufslice &slice)
{
    if (slice.end == slice.start ||
	slice.buf.idx(slice.end-1) != '\0')
	return maybe<const char *>::mknothing();
    return (const char *)slice.buf.linearise(slice.start, slice.end);
}

template <typename typ> maybe<typ>
rx_message::getparam(parameter<typ> p) const
{
    unsigned idx;
    for (idx = 0; idx < nrparams; idx++) {
	if (index[idx].id == p.id)
	    break;
    }
    if (idx == nrparams)
	return maybe<typ>::mknothing();
    bufslice slice(buf,
		   payload_offset + index[idx].offset,
		   idx + 1 == nrparams
		     ? payload_offset + payload_size 
		     : payload_offset + index[idx+1].offset);
    return deserialise<typ>(slice);
}

template maybe<const char *> rx_message::getparam<const char *>(parameter<const char *>)const;

};
