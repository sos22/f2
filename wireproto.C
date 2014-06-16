#include "wireproto.H"

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "buffer.H"
#include "error.H"
#include "fields.H"

#include "fieldfinal.H"
#include "list.tmpl"
#include "wireproto.tmpl"

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
    switch (flavour) {
    case fl_buffer:
        buf.discard(payload_size);
        free(const_cast<idx *>(index));
        break;
    case fl_slice:
        break;
    case fl_clone:
        delete &buf;
        free(const_cast<idx *>(index));
        break;
    }
    delete this;
}

rx_message::rx_message(msgtag _t,
                       sequencenr _sequence,
                       uint16_t _nrparams,
                       size_t _payload_size,
                       size_t _payload_offset,
                       buffer &_buf)
    : index((idx *)calloc(sizeof(index[0]), _nrparams)),
      flavour(fl_buffer),
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
      flavour(fl_slice),
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
                       const struct idx *_index,
                       bool)
    : index(_index),
      flavour(fl_clone),
      sequence(_sequence),
      nrparams(_nrparams),
      payload_size(_payload_size),
      payload_offset(_payload_offset),
      buf(_buf),
      t(_t)
{}

tx_message &
tx_message::addparam(uint16_t id, const void *content, size_t sz)
{
    pinstance p;
    p.id = id;
    if (sz < sizeof(p.internal.content)) {
        p.flavour = pinstance::p_internal;
        p.internal.sz = sz;
        memcpy(p.internal.content, content, sz);
    } else {
        p.flavour = pinstance::p_external;
        p.external.sz = sz;
        p.external.content = malloc(sz);
        memcpy(p.external.content, content, sz);
    }
    params.pushtail(p);
    return *this;
}

template <> tx_message &
tx_message::addparam(parameter<const char *> tmpl, const char *const &val)
{
    return addparam(tmpl.id, val, strlen(val)+1);
}

template <> tx_message &
tx_message::addparam(parameter<bool> tmpl, const bool &val)
{
    return addparam(tmpl.id, &val, sizeof(val));
}

template <> tx_message &
tx_message::addparam(parameter<int> tmpl, const int &val)
{
    return addparam(tmpl.id, &val, sizeof(val));
}

template <> tx_message &
tx_message::addparam(parameter<unsigned> tmpl, const unsigned &val)
{
    return addparam(tmpl.id, &val, sizeof(val));
}

template <> tx_message &
tx_message::addparam(parameter<unsigned short> tmpl, const unsigned short &val)
{
    return addparam(tmpl.id, &val, sizeof(val));
}

template <> tx_message &
tx_message::addparam(parameter<unsigned long> tmpl, const unsigned long &val)
{
    return addparam(tmpl.id, &val, sizeof(val));
}

template <> tx_message &
tx_message::addparam(parameter<double> tmpl, const double &val)
{
    return addparam(tmpl.id, &val, sizeof(val));
}

template <> tx_message &
tx_message::addparam(parameter<tx_compoundparameter> tmpl,
                     const tx_compoundparameter &val)
{
    pinstance p;
    p.id = tmpl.id;
    p.flavour = pinstance::p_compound;
    p.compound = val.clone();
    params.pushtail(p);
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
    case p_internal:
        res.internal.sz = internal.sz;
        memcpy(res.internal.content, internal.content, internal.sz);
        break;
    case p_external:
        res.external.sz = external.sz;
        res.external.content = malloc(external.sz);
        memcpy(res.external.content, external.content, external.sz);
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
    case p_internal:
        return internal.sz;
    case p_external:
        return external.sz;
    case p_compound:
        return compound->serialised_size();
    }
    abort();
}

void
tx_message::pinstance::serialise(buffer &buf) const
{
    switch (flavour) {
    case p_internal:
        buf.queue(internal.content, internal.sz);
        return;
    case p_external:
        buf.queue(external.content, external.sz);
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
    case p_internal:
        internal.sz = o.internal.sz;
        memcpy(internal.content, o.internal.content, internal.sz);
        return;
    case p_external:
        external.sz = o.external.sz;
        external.content = malloc(external.sz);
        memcpy(external.content, o.external.content, external.sz);
        return;
    case p_compound:
        compound = o.compound->clone();
        return;
    }
    abort();
}

tx_message::pinstance::~pinstance()
{
    switch (flavour) {
    case p_internal:
        return;
    case p_external:
        free(external.content);
        return;
    case p_compound:
        delete compound;
        return;
    }
    abort();
}

tx_message::pinstance::pinstance()
    : id(0), flavour(p_internal)
{
    internal.sz = 0;
}

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
template <> maybe<unsigned short>
deserialise(bufslice &slice)
{
    if (slice.end - slice.start != 2)
        return Nothing;
    else
        return *(unsigned short *)slice.buf.linearise(slice.start, slice.end);
}
template <> maybe<unsigned>
deserialise(bufslice &slice)
{
    if (slice.end - slice.start != 4)
        return Nothing;
    else
        return *(unsigned *)slice.buf.linearise(slice.start, slice.end);
}
template <> maybe<unsigned long>
deserialise(bufslice &slice)
{
    if (slice.end - slice.start != 8)
        return Nothing;
    else
        return *(unsigned long *)slice.buf.linearise(slice.start, slice.end);
}
template <> maybe<double>
deserialise(bufslice &slice)
{
    if (slice.end - slice.start != 8)
        return Nothing;
    else
        return *(double *)slice.buf.linearise(slice.start, slice.end);
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

template <> maybe<rx_compoundparameter>
deserialise(bufslice &slice)
{
    rx_compoundparameter res;
    auto r(decode(slice, res));
    if (r.isjust()) return Nothing;
    else return res;
}


template maybe<const char *> rx_message::getparam(parameter<const char *>)const;
template maybe<unsigned short> rx_message::getparam(
    parameter<unsigned short>) const;
template maybe<int> rx_message::getparam(parameter<int>) const;
template maybe<unsigned> rx_message::getparam(parameter<unsigned>) const;
template maybe<unsigned long> rx_message::getparam(parameter<unsigned long>) const;
template maybe<double> rx_message::getparam(parameter<double>) const;
template maybe<error> rx_message::getparam(parameter<error>) const;
template maybe<rx_compoundparameter> rx_message::getparam(
    parameter<rx_compoundparameter>) const;

rx_message *
rx_message::clone() const
{
    idx *newindex = (idx *)calloc(sizeof(newindex[0]), nrparams);
    for (unsigned x = 0; x < nrparams; x++) {
        newindex[x].id = index[x].id;
        newindex[x].offset = index[x].offset - index[0].offset;
    }
    buffer *newbuf = new buffer();
    newbuf->queue(buf.linearise(payload_offset, payload_offset + payload_size),
                  payload_size);
    return new rx_message(t, sequence, nrparams, payload_size,
                          0, *newbuf, newindex, true);
}

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
const parameter<error> err_parameter(0, "error");

template const fields::field &paramfield<unsigned int>(
    const wireproto::rx_message &msg,
    const wireproto::parameter<unsigned int> &);
};

template class list<const wireproto::rx_message *>;
template class list<wireproto::tx_message::pinstance>;

const fields::field &
fields::mk(const wireproto::msgtag &t)
{
    return fields::mk(t.val).nosep();
}

const fields::field &
fields::mk(const wireproto::rx_message *msg)
{
    auto *prefix(&("<rx_message " + fields::mk(msg->t)));
    for (unsigned x = 0;
         x < msg->nrparams;
         x++) {
        prefix = &(*prefix + " ");
        if (x > 14 && msg->nrparams > 16) {
            prefix =
                &(*prefix + "..." + fields::mk(msg->nrparams - x) +
                  " more...");
            break;
        }
        prefix = &(*prefix + fields::mk(msg->index[x].id) + "/" +
                   fields::mk(msg->index[x].offset));
    }
    return *prefix + ">";
}

namespace fields {
template const field &mk<const wireproto::rx_message *>(const orerror<const wireproto::rx_message *> &);
template const field &mk<unsigned int>(
    const wireproto::parameter<unsigned int> &);
};

namespace wireproto {
void
test(class ::test &) {
    buffer buf;
    msgtag t(99);
    parameter<int> p1(5, "p1");
    parameter<const char *> p2(6, "p2");
    {   auto r(tx_message(t).serialise(buf));
        assert(r == Nothing); }
    {   auto rxm(rx_message::fetch(buf));
        assert(rxm.issuccess());
        assert(rxm.success()->t == t);
        rxm.success()->finish(); }
    assert(buf.empty());
    
    {   auto r(tx_message(t)
               .addparam(p1, 73)
               .serialise(buf));
        assert (r == Nothing); }
    {   auto rxm(rx_message::fetch(buf));
        assert(rxm.issuccess());
        assert(rxm.success()->t == t);
        assert(rxm.success()->getparam(p1) != Nothing);
        assert(rxm.success()->getparam(p1).just() == 73);
        rxm.success()->finish(); }
    assert(buf.empty());
    
    {   auto r(tx_message(t)
               .addparam(p1, 73)
               .addparam(p2, "Hello world")
               .serialise(buf));
        assert (r == Nothing); }
    {   auto rxm(rx_message::fetch(buf));
        assert(rxm.issuccess());
        assert(rxm.success()->t == t);
        assert(rxm.success()->getparam(p1) != Nothing);
        assert(rxm.success()->getparam(p1).just() == 73);
        assert(rxm.success()->getparam(p2) != Nothing);
        assert(!strcmp(rxm.success()->getparam(p2).just(),
                       "Hello world"));
        rxm.success()->finish(); }
    assert(buf.empty());
    
    parameter<list<const char * > > p3(7, "p3");
    {   list<const char *> l1;
        l1.pushtail("X");
        l1.pushtail("Y");
        l1.pushtail("Z");
        auto r(tx_message(t)
               .addparam(p3, l1)
               .serialise(buf));
        l1.flush();
        assert (r == Nothing); }
    {   auto rxm(rx_message::fetch(buf));
        assert(rxm.issuccess());
        assert(rxm.success()->t == t);
        assert(rxm.success()->getparam(p1) == Nothing);
        assert(rxm.success()->getparam(p2) == Nothing);
        list<const char *> l2;
        auto fr(rxm.success()->fetch(p3, l2));
        assert(fr == Nothing);
        assert(l2.length() == 3);
        auto it(l2.start());
        assert(!strcmp(*it, "X"));
        it.next();
        assert(!strcmp(*it, "Y"));
        it.next();
        assert(!strcmp(*it, "Z"));
        it.next();
        assert(it.finished());
        l2.flush();
        rxm.success()->finish(); }
    assert(buf.empty());
    
    parameter<tx_compoundparameter> p4t(8, "p4t");
    parameter<rx_compoundparameter> p4r(8, "p4r");
    {   auto r(tx_message(t)
               .addparam(
                   p4t,
                   tx_compoundparameter())
               .addparam(p1, 8)
               .addparam(p2, "root")
               .serialise(buf));
        assert(r == Nothing); }
    {   auto rxm(rx_message::fetch(buf));
        assert(rxm.issuccess());
        assert(rxm.success()->getparam(p1).just() == 8);
        assert(!strcmp(rxm.success()->getparam(p2).just(), "root"));
        auto nested(rxm.success()->getparam(p4r));
        assert(nested != Nothing);
        rxm.success()->finish(); }
    assert(buf.empty());
    
    {   auto r(tx_message(t)
               .addparam(
                   p4t,
                   tx_compoundparameter()
                   .addparam(p1, 7)
                   .addparam(p2, (const char *)"nested"))
               .addparam(p1, 8)
               .addparam(p2, "root")
               .serialise(buf));
        assert(r == Nothing); }
    {   auto rxm(rx_message::fetch(buf));
        assert(rxm.issuccess());
        assert(rxm.success()->getparam(p1).just() == 8);
        assert(!strcmp(rxm.success()->getparam(p2).just(), "root"));
        auto nested(rxm.success()->getparam(p4r));
        assert(nested != Nothing);
        assert(nested.just().getparam(p1).just() == 7);
        assert(!strcmp(nested.just().getparam(p2).just(), "nested"));
        rxm.success()->finish(); }
    assert(buf.empty()); }

template maybe<error> rx_message::fetch(
    parameter<list<const char *> >,
    list<const char *> &) const;
template tx_message &tx_message::addparam(
    parameter<list<const char *> >, const list<const char *> &);

}

template class list<const char *>;
