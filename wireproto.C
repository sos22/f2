#include "wireproto.H"
#include "wireproto.tmpl"

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "buffer.H"
#include "error.H"

#include "list.tmpl"

namespace wireproto {

template <> maybe<error> rx_message::getparam<error>(parameter<error> p) const;

tx_message::tx_message(msgtag _t)
    : t(_t), params()
{}

resp_message::resp_message(const rx_message &o)
    : tx_message(o.t), sequence(o.sequence.reply())
{}

err_resp_message::err_resp_message(const rx_message &o,
				   const error &e)
    : tx_message(o.t),
      sequence(o.sequence.reply())
{
    addparam(wireproto::err_parameter, e);
}

req_message::req_message(msgtag _t, sequencenr _sequence)
    : tx_message(_t), sequence(_sequence)
{}

tx_message::~tx_message()
{
    params.flush();
}

size_t
tx_message::serialised_size() const
{
    size_t sz;
    sz = 8;
    for (auto it(params.start()); !it.finished(); it.next())
	sz += it->serialised_size() + 4;
    return sz;
}

maybe<error>
tx_message::serialise(buffer &buffer, sequencenr snr) const
{
    size_t sz = serialised_size();
    uint16_t nrparams = params.length();

    /* Failure indicator, if present, must be only parameter. */
    for (auto it(params.start()); !it.finished(); it.next())
	if (it->id == wireproto::err_parameter.id)
	    assert(nrparams == 1);

    if (sz >= 0x8000)
	return error::overflowed;

    buffer.queue(&sz, 2);
    buffer.queue(&snr, sizeof(snr));
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
    return Nothing;
}

tx_message *
tx_message::clone() const
{
    tx_message *res = new tx_message(t);
    for (auto it(params.start()); !it.finished(); it.next())
	res->params.pushtail(it->clone());
    return res;
}

maybe<error>
tx_message::serialise(buffer &buffer) const
{
    return serialise(buffer, sequencenr::invalid);
}

maybe<error>
resp_message::serialise(buffer &buffer) const
{
    return tx_message::serialise(buffer, sequence);
}

maybe<error>
err_resp_message::serialise(buffer &buffer) const
{
    return tx_message::serialise(buffer, sequence);
}

maybe<error>
req_message::serialise(buffer &buffer) const
{
    return tx_message::serialise(buffer, sequence);
}

orerror<const rx_message *>
rx_message::fetch(buffer &buffer)
{
    uint16_t sz;
    sequencenr sequence(sequencenr::invalid);
    uint16_t nrparams;
    uint16_t tag;
    if (buffer.avail() < sizeof(sz) + sizeof(nrparams))
	return error::underflowed;
    buffer.fetch(&sz, sizeof(sz));
    if (buffer.avail() + sizeof(sz) < sz) {
	buffer.pushback(&sz, sizeof(sz));
	return error::underflowed;
    }
    if (sz < sizeof(sz) + sizeof(sequence) + sizeof(nrparams) + sizeof(tag))
	return error::invalidmessage;
    buffer.fetch(&sequence, sizeof(sequence));
    buffer.fetch(&nrparams, sizeof(nrparams));
    buffer.fetch(&tag, sizeof(tag));
    if (nrparams > 512)
	return error::overflowed;
    if (sz < 6 + nrparams * 4)
	return error::invalidmessage;
    
    auto work(new rx_message(msgtag(tag),
			     sequence,
			     nrparams,
			     sz - 8 - nrparams * 4,
			     buffer.offset() + 4 * nrparams,
			     buffer));
    buffer.fetch(const_cast<idx *>(work->index), 4 * nrparams);
    for (unsigned i = 0; i < nrparams; i++) {
	if (work->index[i].offset > work->payload_size) {
	    work->finish();
	    return error::invalidmessage;
	}
    }
    auto err(work->getparam(err_parameter));
    if (err.isjust()) {
	work->finish();
	return err.just();
    } else {
	return work;
    }
}

orerror<const rx_message *>
rx_message::fetch(const bufslice &buf)
{
    unsigned long avail = buf.end - buf.start;
    const void *buffer = buf.buf.linearise(buf.start, buf.end);
    if (avail < 8)
	return error::invalidmessage;
    const uint16_t *header = (const uint16_t *)buffer;
    uint16_t sz = header[0];
    if (sz != avail)
	return error::invalidmessage;
    sequencenr sequence(sequencenr::invalid);
    assert(sizeof(sequence) == sizeof(header[1]));
    memcpy(&sequence, &header[1], sizeof(sequence));
    uint16_t nrparams = header[2];
    uint16_t tag = header[3];
    if (nrparams > 128 || sz < 6 + nrparams * 4)
	return error::invalidmessage;
    const struct idx *index = (const struct idx *)((unsigned long)buffer + 8);
    size_t payload_size = avail - 8 - 4 * nrparams;
    for (unsigned i = 0; i < nrparams; i++) {
	if (index[i].offset > payload_size) {
	    return error::invalidmessage;
	}
    }
    auto work(new rx_message(msgtag(tag),
			     sequence,
			     nrparams,
			     payload_size,
			     buf.start + sizeof(header[0]) * 4 + nrparams * 4,
			     buf.buf,
			     index));
    auto err(work->getparam(err_parameter));
    if (err.isjust()) {
	work->finish();
	return err.just();
    } else {
	return work;
    }
}

void
rx_message::finish() const
{
    if (!nonowning) {
	buf.discard(payload_size);
	free(const_cast<idx *>(index));
    }
    delete this;
}

rx_message::rx_message(msgtag _t,
		       sequencenr _sequence,
		       uint16_t _nrparams,
		       size_t _payload_size,
		       size_t _payload_offset,
		       buffer &_buf)
    : index((idx *)calloc(4, _nrparams)),
      nonowning(false),
      sequence(_sequence),
      nrparams(_nrparams),
      payload_size(_payload_size),
      payload_offset(_payload_offset),
      buf(_buf),
      t(_t)
{}

rx_message::rx_message(msgtag _t,
		       sequencenr _sequence,
		       uint16_t _nrparams,
		       size_t _payload_size,
		       size_t _payload_offset,
		       buffer &_buf,
		       const struct idx *_index)
    : index(_index),
      nonowning(true),
      sequence(_sequence),
      nrparams(_nrparams),
      payload_size(_payload_size),
      payload_offset(_payload_offset),
      buf(_buf),
      t(_t)
{}

template <> tx_message &
tx_message::addparam(parameter<const char *> tmpl, const char *const &val)
{
    auto &p(params.append());
    p.id = tmpl.id;
    p.flavour = pinstance::p_string;
    p.string = val;
    return *this;
}

template <> tx_message &
tx_message::addparam(parameter<bool> tmpl, const bool &val)
{
    auto &p(params.append());
    p.id = tmpl.id;
    p.flavour = pinstance::p_bool;
    p.bool_ = val;
    return *this;
}

template <> tx_message &
tx_message::addparam(parameter<int> tmpl, const int &val)
{
    auto &p(params.append());
    p.id = tmpl.id;
    p.flavour = pinstance::p_int32;
    p.int32 = val;
    return *this;
}

template <> tx_message &
tx_message::addparam(parameter<unsigned long> tmpl, const unsigned long &val)
{
    auto &p(params.append());
    p.id = tmpl.id;
    p.flavour = pinstance::p_uint64;
    p.uint64 = val;
    return *this;
}

template <> tx_message &
tx_message::addparam(parameter<tx_compoundparameter> tmpl,
		     const tx_compoundparameter &val)
{
    auto &p(params.append());
    p.id = tmpl.id;
    p.flavour = pinstance::p_compound;
    p.compound = val.clone();
    return *this;
}

tx_message &
tx_message::addparam(parameter<const char *> tmpl, const char *val)
{
    return addparam<const char *>(tmpl, val);
}

tx_message::pinstance
tx_message::pinstance::clone() const
{
    pinstance res;
    res.id = id;
    res.flavour = flavour;
    switch (flavour) {
    case p_string:
	res.string = string;
	break;
    case p_bool:
	res.bool_ = bool_;
	break;
    case p_int32:
	res.int32 = int32;
	break;
    case p_uint64:
	res.uint64 = uint64;
	break;
    case p_compound:
	res.compound = compound->clone();
	break;
    }
    return res;
}

size_t
tx_message::pinstance::serialised_size() const
{
    switch (flavour) {
    case p_string:
	return strlen(string) + 1;
    case p_bool:
	return 1;
    case p_int32:
	return 4;
    case p_uint64:
	return 8;
    case p_compound:
	return compound->serialised_size();
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
    case p_bool:
	buf.queue(&bool_, 1);
	return;
    case p_int32:
	buf.queue(&int32, 4);
	return;
    case p_uint64:
	buf.queue(&uint64, 8);
	return;
    case p_compound:
	compound->content->serialise(buf);
	return;
    }
    abort();
}

tx_message::pinstance::pinstance(wireproto::tx_message::pinstance const&o)
    : id(o.id), flavour(o.flavour)
{
    switch (flavour) {
    case p_string:
	string = o.string;
	break;
    case p_bool:
	bool_ = o.bool_;
	break;
    case p_int32:
	int32 = o.int32;
	break;
    case p_uint64:
	uint64 = o.uint64;
	break;
    case p_compound:
	compound = o.compound->clone();
	break;
    }
}

tx_message::pinstance::~pinstance()
{
    switch (flavour) {
    case p_string:
	break;
    case p_bool:
	break;
    case p_int32:
	break;
    case p_uint64:
	break;
    case p_compound:
	delete compound;
	break;
    }
}

tx_message::pinstance::pinstance()
    : id(0), flavour(p_int32)
{}

template <> maybe<const char *>
deserialise(bufslice &slice)
{
    if (slice.end == slice.start ||
	slice.buf.idx(slice.end-1) != '\0')
	return Nothing;
    return (const char *)slice.buf.linearise(slice.start, slice.end);
}
template <> maybe<int>
deserialise(bufslice &slice)
{
    if (slice.end - slice.start != 4)
	return Nothing;
    else
	return *(int *)slice.buf.linearise(slice.start, slice.end);
}
template <> maybe<unsigned long>
deserialise(bufslice &slice)
{
    if (slice.end - slice.start != 8)
	return Nothing;
    else
	return *(unsigned long *)slice.buf.linearise(slice.start, slice.end);
}
template <> maybe<bool>
deserialise(bufslice &slice)
{
    if (slice.end - slice.start != 1)
	return Nothing;
    else
	return *(bool *)slice.buf.linearise(slice.start, slice.end);
}

template <typename typ> maybe<error> decode(bufslice &slice,
					    typ &out);

template <> maybe<error>
decode(bufslice &slice, rx_compoundparameter &out)
{
    auto r(rx_message::fetch(slice));
    if (r.isfailure()) return r.failure();
    assert(out.content == NULL);
    out.content = r.success();
    return Nothing;
}

template maybe<const char *> rx_message::getparam<const char *>(parameter<const char *>)const;
template maybe<int> rx_message::getparam<int>(parameter<int>) const;
template maybe<unsigned long> rx_message::getparam<unsigned long>(parameter<unsigned long>) const;
template maybe<error> rx_message::getparam<error>(parameter<error>) const;

template <> maybe<error>
rx_message::fetch(parameter<rx_compoundparameter> p, rx_compoundparameter &out) const
{
    unsigned x;
    for (x = 0; x < nrparams; x++) {
	if (index[x].id == p.id)
	    break;
    }
    if (x == nrparams)
	return error::from_errno(ENOENT);
    bufslice slice(buf,
		   payload_offset + index[x].offset,
		   x + 1 == nrparams
		     ? payload_offset + payload_size 
		     : payload_offset + index[x+1].offset);
    return decode(slice, out);
}

orerror<rx_compoundparameter *>
rx_compoundparameter::fetch(const bufslice &o)
{
    auto r(rx_message::fetch(o));
    if (r.isfailure()) return r.failure();
    else return new rx_compoundparameter(r.success());
}

const sequencenr sequencenr::invalid(0);
const parameter<error> err_parameter(0);

};

template class list<const wireproto::rx_message *>;
template class list<wireproto::tx_message::pinstance>;
