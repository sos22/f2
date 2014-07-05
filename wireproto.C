#include "wireproto.H"

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "buffer.H"
#include "error.H"
#include "fields.H"
#include "test.H"

#include "wireproto.tmpl"
#include "list.tmpl"

#include "fieldfinal.H"

namespace wireproto {

template <> maybe<error> rx_message::getparam<error>(parameter<error> p) const;

tx_message::tx_message(msgtag _t)
    : t(_t), params()
{}

resp_message::resp_message(const rx_message &o)
    : tx_message(o.tag()), sequence(o.sequence().reply())
{}

err_resp_message::err_resp_message(const rx_message &o,
                                   const error &e)
    : tx_message(o.tag()),
      sequence(o.sequence().reply())
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

    unsigned initavail(buffer.avail());
    wireheader hdr(sz, snr, nrparams, t);
    buffer.queue(&hdr, sizeof(hdr));
    uint16_t consumed = sizeof(hdr) + sizeof(index) * nrparams;
    for (auto it(params.start()); !it.finished(); it.next()) {
        index idx(it->id, consumed);
        buffer.queue(&idx, sizeof(idx));
        consumed += it->serialised_size();
    }
    for (auto it(params.start()); !it.finished(); it.next())
        it->serialise(buffer);
    assert(buffer.avail() == initavail + consumed);
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
tx_message::addparam(parameter<char> tmpl, const char &val)
{
    return addparam(tmpl.id, &val, sizeof(val));
}

template <> tx_message &
tx_message::addparam(parameter<unsigned char> tmpl, const unsigned char &val)
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
tx_message::addparam(parameter<short> tmpl, const short &val)
{
    return addparam(tmpl.id, &val, sizeof(val));
}

template <> tx_message &
tx_message::addparam(parameter<unsigned long> tmpl, const unsigned long &val)
{
    return addparam(tmpl.id, &val, sizeof(val));
}

template <> tx_message &
tx_message::addparam(parameter<long> tmpl, const long &val)
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
#if COVERAGE
    default:
#endif
    case p_compound:
        return compound->serialised_size();
    }
#if !COVERAGE
    abort();
#endif
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
#if COVERAGE
    default:
#endif
    case p_compound:
        compound->content->serialise(buf);
        return;
    }
#if !COVERAGE
    abort();
#endif
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
#if COVERAGE
    default:
#endif
    case p_compound:
        compound = o.compound->clone();
        return;
    }
#if !COVERAGE
    abort();
#endif
}

tx_message::pinstance::~pinstance()
{
    switch (flavour) {
    case p_internal:
        return;
    case p_external:
        free(external.content);
        return;
#if COVERAGE
    default:
#endif
    case p_compound:
        delete compound;
        return;
    }
#if !COVERAGE
    abort();
#endif
}

tx_message::pinstance::pinstance()
    : id(0), flavour(p_internal)
{
    internal.sz = 0;
}

rx_message::rx_message(const rx_message &o)
    : msg(o.owning
          ? (struct wireheader *)malloc(o.msg->sz)
          : o.msg),
      owning(o.owning) {
    if (owning) memcpy((void *)msg, o.msg, o.msg->sz); }

rx_message::rx_message(rx_message &&o)
    : msg(o.msg),
      owning(o.owning) {
    o.msg = NULL; }

rx_message::rx_message(const wireheader *_msg,
                       bool _owning)
    : msg(_msg), owning(_owning) {}

orerror<rx_message>
rx_message::parse(const wireheader *msg,
                  size_t sz,
                  bool owning) {
    if (sz < sizeof(*msg)) return error::invalidmessage;
    if (msg->sz != sz) return error::invalidmessage;
    if (msg->nrparams > 512) return error::overflowed;
    size_t minsize = sizeof(*msg) + msg->nrparams * sizeof(msg->idx[0]);
    if (sz < minsize || (msg->nrparams == 0 && sz != sizeof(*msg))) {
        return error::invalidmessage; }
    for (unsigned i = 0; i < msg->nrparams; i++) {
        if (msg->idx[i].offset < minsize ||
            msg->idx[i].offset > msg->sz ||
            (i != 0 && msg->idx[i].offset < msg->idx[i-1].offset)) {
            return error::invalidmessage; } }
    return rx_message(msg, owning); }

orerror<rx_message>
rx_message::fetch(buffer &buffer) {
    uint16_t sz;
    if (buffer.avail() < sizeof(wireheader)) return error::underflowed;
    buffer.fetch(&sz, sizeof(sz));
    if (buffer.avail() + sizeof(sz) < sz) {
        buffer.pushback(&sz, sizeof(sz));
        return error::underflowed; }
    wireheader *msg = (wireheader *)malloc(sz);
    msg->sz = sz;
    buffer.fetch((void *)((unsigned long)msg + 2), sz - 2);
    auto res(parse(msg, sz, true));
    if (res.isfailure()) {
        free(msg);
        return res.failure(); }
    auto err(res.success().getparam(err_parameter));
    if (err.isjust()) return err.just();
    else return res.success(); }

orerror<rx_message>
rx_message::fetch(const bufslice &buf) {
    auto res(parse((const struct wireheader *)buf.content, buf.sz, false));
    if (res.isfailure()) return res.failure();
    auto err(res.success().getparam(err_parameter));
    if (err.isjust()) return err.just();
    else return res.success(); }

rx_message *
rx_message::steal() {
    auto res(new rx_message(msg, owning));
    msg = NULL;
    return res; }

bool
rx_message::isreply() const {
    return sequence().isreply(); }

sequencenr
rx_message::sequence() const {
    return msg->seq; }

msgtag
rx_message::tag() const {
    return msg->tag; }

rx_message::status_t
rx_message::status() const {
    return status_t(msg->tag); }

rx_message::~rx_message() {
    if (owning) free((void *)msg); }

/* Note that this returns a pointer to the passed-in buffer, so
   deserialised strings are only valid for as long as the message from
   which they were deserialised. */
template <> maybe<const char *>
deserialise(bufslice &slice) {
    const char *res = (const char *)slice.content;
    if (slice.sz == 0 || res[slice.sz-1] != '\0') return Nothing;
    else return res; }
template <> maybe<rx_message>
deserialise(bufslice &slice) {
    auto r(rx_message::fetch(slice));
    if (r.isfailure()) return Nothing;
    else return r.success(); }
template <> maybe<bool>
deserialise(bufslice &slice) {
    if (slice.sz != 1) return Nothing;
    unsigned char res(*(unsigned char *)slice.content);
    if (res == 0) return false;
    else if (res == 1) return true;
    else return Nothing; }
template <> maybe<char>
deserialise(bufslice &slice) {
    if (slice.sz != 1) return Nothing;
    else return *(char *)slice.content; }
template <> maybe<unsigned char>
deserialise(bufslice &slice) {
    if (slice.sz != 1) return Nothing;
    else return *(unsigned char *)slice.content; }
template <> maybe<short>
deserialise(bufslice &slice) {
    if (slice.sz != 2) return Nothing;
    else return *(short *)slice.content; }
template <> maybe<unsigned short>
deserialise(bufslice &slice) {
    if (slice.sz != 2) return Nothing;
    else return *(unsigned short *)slice.content; }
template <> maybe<int>
deserialise(bufslice &slice) {
    if (slice.sz != 4) return Nothing;
    else return *(int *)slice.content; }
template <> maybe<unsigned>
deserialise(bufslice &slice) {
    if (slice.sz != 4) return Nothing;
    else return *(unsigned *)slice.content; }
template <> maybe<long>
deserialise(bufslice &slice) {
    if (slice.sz != 8) return Nothing;
    else return *(long *)slice.content; }
template <> maybe<unsigned long>
deserialise(bufslice &slice) {
    if (slice.sz != 8) return Nothing;
    else return *(unsigned long *)slice.content; }
template <> maybe<double>
deserialise(bufslice &slice) {
    if (slice.sz != 8) return Nothing;
    else return *(double *)slice.content; }

template <> maybe<msgtag>
deserialise(bufslice &slice) {
    auto r(deserialise<uint16_t>(slice));
    if (!r) return Nothing;
    else return msgtag(r.just()); }

template maybe<const char *> rx_message::getparam(parameter<const char *>)const;
template maybe<unsigned short> rx_message::getparam(
    parameter<unsigned short>) const;
template maybe<int> rx_message::getparam(parameter<int>) const;
template maybe<unsigned> rx_message::getparam(parameter<unsigned>) const;
template maybe<long> rx_message::getparam(parameter<long>) const;
template maybe<unsigned long> rx_message::getparam(parameter<unsigned long>) const;
template maybe<double> rx_message::getparam(parameter<double>) const;
template maybe<error> rx_message::getparam(parameter<error>) const;
template maybe<rx_message> rx_message::getparam(
    parameter<rx_message>) const;

const sequencenr sequencenr::invalid(0);
const parameter<error> err_parameter(0);
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
    auto *prefix(&("<rx_message " + fields::mk(msg->tag())));
    for (unsigned x = 0;
         x < msg->msg->nrparams;
         x++) {
        prefix = &(*prefix + " ");
        if (x > 14 && msg->msg->nrparams > 16) {
            prefix =
                &(*prefix + "..." + fields::mk(msg->msg->nrparams - x) +
                  " more...");
            break;
        }
        prefix = &(*prefix + fields::mk(msg->msg->idx[x].id) + "/" +
                   fields::mk(msg->msg->idx[x].offset));
    }
    return *prefix + ">";
}

namespace fields {
template const field &mk<const wireproto::rx_message *>(const orerror<const wireproto::rx_message *> &);
template const field &mk<unsigned int>(
    const wireproto::parameter<unsigned int> &);
template const field &mk<int>(const wireproto::parameter<int> &);
};

namespace wireproto {
template maybe<bool> rx_message::getparam(parameter<bool>) const;
template maybe<char> rx_message::getparam(parameter<char>) const;
template maybe<unsigned char> rx_message::getparam(
    parameter<unsigned char>) const;
template maybe<short> rx_message::getparam(
    parameter<short>) const;
template maybe<error> rx_message::fetch(
    parameter<list<const char *> >,
    list<const char *> &) const;
template tx_message &tx_message::addparam(
    parameter<list<const char *> >, const list<const char *> &);
template tx_message &tx_message::addparam(
    parameter<list<rx_message::status_t> >,
    const list<rx_message::status_t> &);
template tx_message& tx_message::addparam(
    parameter<list<msgtag> >, list<msgtag> const&);
template maybe<error> rx_message::fetch(
    parameter<list<rx_message::status_t> >,
    list<rx_message::status_t> &) const;
template maybe<error> rx_message::fetch(
    parameter<list<msgtag> >, list<msgtag>&) const;
template maybe<error> rx_message::fetch<int>(
    parameter<list<int> >, list<int>&) const;
template <> maybe<rx_message::status_t> deserialise(
    wireproto::bufslice &slice) {
    auto r(deserialise<msgtag>(slice));
    if (!r) return Nothing;
    else return rx_message::status_t(r.just()); } }

wireproto_simple_wrapper_type(wireproto::sequencer::status_t, uint16_t, nextseq)
wireproto_simple_wrapper_type(wireproto::msgtag, uint16_t, val)
wireproto_simple_wrapper_type(wireproto::rx_message::status_t, msgtag, t)

const fields::field &
fields::mk(const wireproto::sequencer::status_t &o) {
    return "<nextseq:" + mk(o.nextseq) + ">"; }

const fields::field &
fields::mk(const wireproto::rx_message::status_t &o) {
    return "<tag:" + mk(o.t) + ">"; }

template class list<const char *>;
template list<wireproto::rx_message::status_t>::list();
template list<wireproto::rx_message::status_t>::list(
    const list<wireproto::rx_message::status_t> &o);
template list<wireproto::rx_message::status_t>::list(
    list<wireproto::rx_message::status_t> &&o);
template bool list<wireproto::rx_message::status_t>::empty() const;
template void list<wireproto::rx_message::status_t>::flush();
template void list<wireproto::rx_message::status_t>::pushtail(
    const wireproto::rx_message::status_t &);
template list<wireproto::rx_message::status_t>::iter
    list<wireproto::rx_message::status_t>::start();
template list<wireproto::rx_message::status_t>::iter::iter(
    list<wireproto::rx_message::status_t> *, bool);
template wireproto::rx_messagestatus &
    list<wireproto::rx_messagestatus>::iter::operator*();
template void list<wireproto::rx_messagestatus>::iter::next();
template void list<wireproto::rx_messagestatus>::iter::remove();
template bool list<wireproto::rx_messagestatus>::iter::finished() const;
template list<wireproto::rx_messagestatus>::const_iter
    list<wireproto::rx_messagestatus>::start() const;
template list<wireproto::rx_message::status_t>::const_iter::const_iter(
    const list<wireproto::rx_message::status_t> *, bool);
template const wireproto::rx_messagestatus &
    list<wireproto::rx_messagestatus>::const_iter::operator*() const;
template void list<wireproto::rx_messagestatus>::const_iter::next();
template bool list<wireproto::rx_messagestatus>::const_iter::finished() const;
template list<wireproto::rx_message::status_t>::~list();
template wireproto::rx_message::status_t std::function<
    wireproto::rx_message::status_t (
        wireproto::rx_message const* const&)>::operator()
    (wireproto::rx_message const* const&) const;
template list<wireproto::msgtag>::list();
template bool list<wireproto::msgtag>::empty() const;
template void list<wireproto::msgtag>::pushtail(wireproto::msgtag const&);
template list<wireproto::msgtag>::iter list<wireproto::msgtag>::start();
template list<wireproto::msgtag>::iter::iter(list<wireproto::msgtag>*, bool);
template wireproto::msgtag &list<wireproto::msgtag>::iter::operator*();
template void list<wireproto::msgtag>::iter::remove();
template void list<wireproto::msgtag>::iter::next();
template bool list<wireproto::msgtag>::iter::finished() const;
template list<wireproto::msgtag>::const_iter
    list<wireproto::msgtag>::start() const;
template list<wireproto::msgtag>::const_iter::const_iter(
    list<wireproto::msgtag> const*, bool);
template const wireproto::msgtag &
    list<wireproto::msgtag>::const_iter::operator*() const;
template void list<wireproto::msgtag>::const_iter::next();
template bool list<wireproto::msgtag>::const_iter::finished() const;
template void list<wireproto::msgtag>::flush();
template list<wireproto::msgtag>::~list();

namespace fields {
template const field &mk(const list<wireproto::rx_message::status_t> &);
}

void
tests::wireproto() {
    using namespace wireproto;
    msgtag t(99);
    testcaseV("wireproto", "empty", [t] () {
            ::buffer buf;
            {   auto r(tx_message(t).serialise(buf));
                assert(r == Nothing); }
            {   auto rxm(rx_message::fetch(buf));
                assert(rxm.issuccess());
                assert(rxm.success().tag() == t); }
            assert(buf.empty()); });

    testcaseV("wireproto", "missing", [t] () {
            ::buffer buf;
            parameter<int> p1(5);
            {   auto r(tx_message(t)
                       .serialise(buf));
                assert (r == Nothing); }
            {   auto rxm(rx_message::fetch(buf));
                assert(rxm.issuccess());
                assert(rxm.success().tag() == t);
                assert(rxm.success().getparam(p1) == Nothing); }
            assert(buf.empty());});

    testcaseV("wireproto", "intparam", [t] () {
            ::buffer buf;
            parameter<int> p1(5);
            parameter<unsigned> p2(6);
            {   auto r(tx_message(t)
                       .addparam(p1, 73)
                       .addparam(p2, 3u)
                       .serialise(buf));
                assert (r == Nothing); }
            {   auto rxm(rx_message::fetch(buf));
                assert(rxm.issuccess());
                assert(rxm.success().tag() == t);
                assert(rxm.success().getparam(p1) != Nothing);
                assert(rxm.success().getparam(p1).just() == 73);
                assert(rxm.success().getparam(p2).just() == 3); }
            assert(buf.empty());});

    testcaseV("wireproto", "boolparam", [t] () {
            ::buffer buf;
            parameter<bool> p1(5);
            parameter<bool> p2(6);
            parameter<bool> p3(7);
            {   auto r(tx_message(t)
                       .addparam(p1, true)
                       .addparam(p2, false)
                       .addparam(parameter<unsigned char>(p3), (unsigned char)3)
                       .serialise(buf));
                assert (r == Nothing); }
            {   auto rxm(rx_message::fetch(buf));
                assert(rxm.issuccess());
                assert(rxm.success().tag() == t);
                assert(rxm.success().getparam(p1).just() == true);
                assert(rxm.success().getparam(p2).just() == false);
                assert(rxm.success().getparam(p3) == Nothing); }
            assert(buf.empty());});

    testcaseV("wireproto", "charparam", [t] () {
            ::buffer buf;
            parameter<char> p1(5);
            parameter<unsigned char> p2(6);
            parameter<char> p3(7);
            {   auto r(tx_message(t)
                       .addparam(p1, 'a')
                       .addparam(p2, (unsigned char)8)
                       .addparam(parameter<int>(p3), 7)
                       .serialise(buf));
                assert (r == Nothing); }
            {   auto rxm(rx_message::fetch(buf));
                assert(rxm.issuccess());
                assert(rxm.success().tag() == t);
                assert(rxm.success().getparam(p1).just() == 'a');
                assert(rxm.success().getparam(p2).just() == 8);
                assert(rxm.success().getparam(p3) == Nothing); }
            assert(buf.empty());});

    testcaseV("wireproto", "shortparam", [t] () {
            ::buffer buf;
            parameter<short> p1(5);
            parameter<unsigned short> p2(6);
            {   auto r(tx_message(t)
                       .addparam(p1, (short)-93)
                       .addparam(p2, (unsigned short)2)
                       .serialise(buf));
                assert (r == Nothing); }
            {   auto rxm(rx_message::fetch(buf));
                assert(rxm.issuccess());
                assert(rxm.success().tag() == t);
                assert(rxm.success().getparam(p1).just() == -93);
                assert(rxm.success().getparam(p2).just() == 2); }
            assert(buf.empty());});

    testcaseV("wireproto", "longparam", [t] () {
            ::buffer buf;
            parameter<long> p1(5);
            parameter<unsigned long> p2(6);
            {   auto r(tx_message(t)
                       .addparam(p1, -0x10000000000l)
                       .addparam(p2, 0xffe51340000ul)
                       .serialise(buf));
                assert (r == Nothing); }
            {   auto rxm(rx_message::fetch(buf));
                assert(rxm.issuccess());
                assert(rxm.success().tag() == t);
                assert(rxm.success().getparam(p1).just() == -0x10000000000l);
                assert(rxm.success().getparam(p2).just() == 0xffe51340000ul); }
            assert(buf.empty());});

    testcaseV("wireproto", "doubleparam", [t] () {
            ::buffer buf;
            parameter<double> p1(1);
            {   auto r(tx_message(t)
                       .addparam(p1, 7.)
                       .serialise(buf));
                assert (r == Nothing); }
            {   auto rxm(rx_message::fetch(buf));
                assert(rxm.issuccess());
                assert(rxm.success().tag() == t);
                assert(rxm.success().getparam(p1).just() == 7); }
            assert(buf.empty());});

    testcaseV("wireproto", "strparam", [t] () {
            ::buffer buf;
            parameter<int> p1(5);
            parameter<const char *> p2(6);
            {   auto r(tx_message(t)
                       .addparam(p1, 73)
                       .addparam(p2, "Hello world")
                       .serialise(buf));
                assert (r == Nothing); }
            {   auto rxm(rx_message::fetch(buf));
                assert(rxm.issuccess());
                assert(rxm.success().tag() == t);
                assert(rxm.success().getparam(p1) != Nothing);
                assert(rxm.success().getparam(p1).just() == 73);
                assert(rxm.success().getparam(p2) != Nothing);
                assert(!strcmp(rxm.success().getparam(p2).just(),
                               "Hello world")); }
            assert(buf.empty()); });

    testcaseV("wireproto", "strlist", [t] () {
            ::buffer buf;
            parameter<list<const char * > > p3(7);
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
                assert(rxm.success().tag() == t);
                list<const char *> l2;
                auto fr(rxm.success().fetch(p3, l2));
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
                l2.flush(); }
            assert(buf.empty()); });

    testcaseV("wireproto", "emptycompound", [t] () {
            ::buffer buf;
            parameter<int> p1(5);
            parameter<const char *> p2(6);
            parameter<tx_compoundparameter> p4t(8);
            parameter<rx_message> p4r(8);
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
                assert(rxm.success().getparam(p1).just() == 8);
                assert(!strcmp(rxm.success().getparam(p2).just(), "root"));
                auto nested(rxm.success().getparam(p4r));
                assert(nested != Nothing); }
            assert(buf.empty()); });

    testcaseV("wireproto", "usedcompound", [t] () {
            ::buffer buf;
            parameter<tx_compoundparameter> p4t(8);
            parameter<rx_message> p4r(8);
            parameter<int> p1(5);
            parameter<const char *> p2(6);
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
                assert(rxm.success().getparam(p1).just() == 8);
                assert(!strcmp(rxm.success().getparam(p2).just(), "root"));
                auto nested(rxm.success().getparam(p4r));
                assert(nested != Nothing);
                assert(nested.just().getparam(p1).just() == 7);
                assert(!strcmp(nested.just().getparam(p2).just(), "nested")); }
            assert(buf.empty()); } );

    testcaseV("wireproto", "reqmessage", [t] () {
            ::buffer buf;
            parameter<int> p1(12);
            sequencer s;
            auto snr(s.get());
            req_message(t, snr).addparam(p1, 99).serialise(buf);
            auto rxm(rx_message::fetch(buf));
            assert(buf.empty());
            assert(rxm.success().tag() == t);
            assert(rxm.success().sequence() == snr);
            assert(rxm.success().getparam(p1).isjust());
            assert(rxm.success().getparam(p1).just() == 99);
            parameter<int> p2(13);
            resp_message(rxm.success()).addparam(p2, 11).serialise(buf);
            auto rxm2(rx_message::fetch(buf));
            assert(buf.empty());
            assert(rxm2.success().tag() == t);
            assert(rxm2.success().isreply());
            assert(rxm2.success().sequence() == snr.reply());
            assert(rxm2.success().getparam(p1) == Nothing);
            assert(rxm2.success().getparam(p2).isjust());
            assert(rxm2.success().getparam(p2).just() == 11); });

    testcaseV("wireproto", "errresp", [t] () {
            ::buffer buf;
            auto snr(sequencer().get());
            req_message(t, snr).serialise(buf);
            auto rxm(rx_message::fetch(buf));
            err_resp_message(rxm.success(), error::ratelimit).serialise(buf);
            auto rxm2(rx_message::fetch(buf));
            assert(rxm2.isfailure());
            assert(rxm2.failure() == error::ratelimit); });

    testcaseV("wireproto", "external", [t] () {
            ::buffer buf;
            parameter<const char *> s(1);
            char ss[5001];
            for (int i = 0; i < 5000; i++) {
                ss[i] = "qwertyuiopasdfghjklzxcvbnm"[(i*7)%26]; }
            ss[5000] = 0;
            tx_message(t).addparam(s, ss).serialise(buf);
            auto rxm(rx_message::fetch(buf));
            assert(buf.empty());
            assert(rxm.issuccess());
            auto p(rxm.success().getparam(s));
            assert(p.isjust());
            assert(!strcmp(p.just(), ss));});

    testcaseV("wireproto", "externalinner", [t] () {
            ::buffer buf;
            parameter<const char *> s(1);
            parameter<tx_compoundparameter> txp(1);
            parameter<rx_message> rxp(1);
            char ss[5001];
            for (int i = 0; i < 5000; i++) {
                ss[i] = "qwertyuiopasdfghjklzxcvbnm"[(i*7)%26]; }
            ss[5000] = 0;
            const char *sss(ss);
            tx_message(t)
                .addparam(
                    txp,
                    tx_compoundparameter()
                    .addparam(s, sss))
                .serialise(buf);
            auto rxm(rx_message::fetch(buf));
            assert(buf.empty());
            assert(rxm.issuccess());
            auto pp(rxm.success().getparam(rxp));
            assert(pp.isjust());
            auto ppp(pp.just().getparam(s));
            assert(ppp.isjust());
            assert(!strcmp(ppp.just(), ss));});

    testcaseV("wireproto", "innerinner", [t] () {
            ::buffer buf;
            parameter<tx_compoundparameter> txouter(1);
            parameter<rx_message> rxouter(1);
            parameter<tx_compoundparameter> txmid(1);
            parameter<rx_message> rxmid(1);
            parameter<int> inner(1);
            tx_message(t)
                .addparam(
                    txouter,
                    tx_compoundparameter()
                        .addparam(
                            txmid,
                            tx_compoundparameter()
                                .addparam(inner, 5)))
                .serialise(buf);
            auto rxm(rx_message::fetch(buf));
            assert(buf.empty());
            auto outer(rxm.success().getparam(rxouter));
            auto mid(outer.just().getparam(rxmid));
            auto res(mid.just().getparam(inner));
            assert(res.just() == 5); });

    testcaseV("wireproto", "underflow", [t] () {
            ::buffer buf;
            parameter<int> p(1);
            tx_message(t).addparam(p, 5).serialise(buf);
            ::buffer b2;
            b2.queue(buf.linearise(0, buf.avail() - 1), buf.avail() - 1);
            auto rxm(rx_message::fetch(b2));
            assert(rxm.failure() == error::underflowed);
            b2.queue(buf.linearise(buf.avail() - 1, buf.avail()), 1);
            auto rxm2(rx_message::fetch(b2));
            assert(rxm2.success().getparam(p).just() == 5); });

    testcaseV("wireproto", "badmessage", [t] () {
            ::buffer buf;
            wireheader h(sizeof(h), sequencenr::invalid, 1, msgtag(5));
            buf.queue(&h, sizeof(h));
            auto rxm(rx_message::fetch(buf));
            assert(rxm.failure() == error::invalidmessage); });

    testcaseV("wireproto", "steal", [t] () {
            ::buffer buf;
            parameter<const char *> p(1);
            tx_message(t).addparam(p, "HELLO").serialise(buf);
            rx_message *r;
            {  auto rxm(rx_message::fetch(buf));
                r = rxm.success().steal(); }
            assert(r);
            assert(!strcmp(r->getparam(p).just(), "HELLO"));
            delete r; });

    testcaseV("wireproto", "rxstatus", [t] () {
            rx_message::status_t stat;
            {   ::buffer buf;
                {   parameter<int> p(1);
                    tx_message(t).addparam(p, 5).serialise(buf); }
                auto rxm(rx_message::fetch(buf));
                stat = rxm.success().status(); }
            {   fields::fieldbuf buf;
                fields::mk(stat).fmt(buf);
                assert(!strcmp(buf.c_str(), "<tag:99>")); }
            parameter<rx_message::status_t> p(2);
            ::buffer buf;
            tx_message(t).addparam(p, stat).serialise(buf);
            auto rxm(rx_message::fetch(buf));
            assert(rxm.success().getparam(p).just() == stat); });

    testcaseV("wireproto", "rxfield", [t] () {
            rx_message::status_t stat;
            ::buffer buf;
            parameter<int> p(1);
            parameter<int> p2(73);
            tx_message(t).addparam(p, 5).addparam(p2, 99).serialise(buf);
            auto rxm(rx_message::fetch(buf));
            fields::fieldbuf fb;
            fields::mk(&rxm.success()).fmt(fb);
            assert(!strcmp(fb.c_str(), "<rx_message 99 1/16 73/20>")); });

    testcaseV("wireproto", "rxfield2", [t] () {
            rx_message::status_t stat;
            ::buffer buf;
            tx_message txm(t);
            for (int i = 1; i < 30; i++) {
                parameter<char> p(i);
                txm.addparam(p, (char)i); }
            txm.serialise(buf);
            auto rxm(rx_message::fetch(buf));
            fields::fieldbuf fb;
            fields::mk(&rxm.success()).fmt(fb);
            assert(!strcmp(fb.c_str(),
                           "<rx_message 99 1/124 2/125 3/126 4/127 5/128 "
                           "6/129 7/130 8/131 9/132 10/133 11/134 12/135 "
                           "13/136 14/137 15/138 ...14 more...>")); });

    testcaseV("wireproto", "sequencerstatus", [t] () {
            sequencer snr;
            snr.get();
            snr.get();
            parameter<sequencer::status_t> p(1);
            ::buffer buf;
            tx_message(t).addparam(p, snr.status()).serialise(buf);
            auto rxm(rx_message::fetch(buf));
            fields::fieldbuf fb;
            fields::mk(rxm.success().getparam(p).just()).fmt(fb);
            assert(!strcmp(fb.c_str(), "<nextseq:3>")); });

    testcaseV("wireproto", "paramfield", [t] () {
            fields::fieldbuf fb;
            fields::mk(parameter<int>(7)).fmt(fb);
            assert(!strcmp(fb.c_str(), "<param:7>"));});

    testcaseV("wireproto", "badlist", [t] () {
            parameter<list<int> > p(1);
            tx_message txm(t);
            txm.addparam(parameter<int>(p), 5);
            txm.addparam(parameter<int>(p), 6);
            txm.addparam(parameter<int>(p), 7);
            txm.addparam(parameter<char>(p), 'a');
            txm.addparam(parameter<int>(p), 9);
            ::buffer buf;
            txm.serialise(buf);
            auto rxm(rx_message::fetch(buf));
            list<int> l;
            auto fr(rxm.success().fetch(p, l));
            assert(fr.just() == error::invalidmessage);
            assert(l.empty()); });

    testcaseV("wireproto", "msgtaglist", [t] () {
            parameter<list<msgtag> > p(1);
            ::buffer buf;
            list<msgtag> l;
            l.pushtail(msgtag(1));
            l.pushtail(msgtag(2));
            tx_message(t).addparam(p, l).serialise(buf);
            auto rxm(rx_message::fetch(buf));
            list<msgtag> l2;
            auto fr(rxm.success().fetch(p, l2));
            assert(fr == Nothing);
            auto it1(l.start());
            auto it2(l2.start());
            while (1) {
                assert(it1.finished() == it2.finished());
                if (it1.finished()) break;
                assert(*it1 == *it2);
                it1.next();
                it2.next(); }
            l.flush();
            l2.flush(); });

    testcaseV("wireproto", "msgstatuslist", [t] () {
            parameter<list<rx_message::status_t> > p(1);
            ::buffer buf;
            list<rx_message::status_t> l;
            l.pushtail(rx_message::status_t(msgtag(7)));
            tx_message(t).addparam(p, l).serialise(buf);
            auto rxm(rx_message::fetch(buf));
            list<rx_message::status_t> l2;
            auto fr(rxm.success().fetch(p, l2));
            assert(fr == Nothing);
            auto it1(l.start());
            auto it2(l2.start());
            while (1) {
                assert(it1.finished() == it2.finished());
                if (it1.finished()) break;
                assert(*it1 == *it2);
                it1.next();
                it2.next(); }
            l.flush();
            l2.flush(); });
}
