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
};

namespace tests {
void
wireproto() {
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
            parameter<int> p1(5, "p1");
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
            parameter<int> p1(5, "p1");
            {   auto r(tx_message(t)
                       .addparam(p1, 73)
                       .serialise(buf));
                assert (r == Nothing); }
            {   auto rxm(rx_message::fetch(buf));
                assert(rxm.issuccess());
                assert(rxm.success().tag() == t);
                assert(rxm.success().getparam(p1) != Nothing);
                assert(rxm.success().getparam(p1).just() == 73); }
            assert(buf.empty());});

    testcaseV("wireproto", "strparam", [t] () {
            ::buffer buf;
            parameter<int> p1(5, "p1");
            parameter<const char *> p2(6, "p2");
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
            parameter<int> p1(5, "p1");
            parameter<const char *> p2(6, "p2");
            parameter<tx_compoundparameter> p4t(8, "p4t");
            parameter<rx_message> p4r(8, "p4r");
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
            parameter<tx_compoundparameter> p4t(8, "p4t");
            parameter<rx_message> p4r(8, "p4r");
            parameter<int> p1(5, "p1");
            parameter<const char *> p2(6, "p2");
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
            assert(buf.empty()); } ); } }

namespace wireproto {
template maybe<error> rx_message::fetch(
    parameter<list<const char *> >,
    list<const char *> &) const;
template tx_message &tx_message::addparam(
    parameter<list<const char *> >, const list<const char *> &);
template tx_message &tx_message::addparam(
    parameter<list<rx_message::status_t> >,
    const list<rx_message::status_t> &);
template maybe<error> rx_message::fetch(
    parameter<list<rx_message::status_t> >,
    list<rx_message::status_t> &) const;
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

namespace fields {
template const field &mk(const list<wireproto::rx_message::status_t> &);
}
