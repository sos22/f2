#include "buffer.H"

#include "fd.H"
#include "proto2.H"
#include "pubsub.H"
#include "serialise.H"
#include "util.H"

buffer::subbuf *
buffer::newsubbuf(size_t sz, bool insert)
{
    subbuf *res;
    size_t newsz;
    for (newsz = 16384; newsz < sz + sizeof(*res); newsz *= 2)
        ;
    res = (subbuf *)malloc(newsz);
    new (res) subbuf();
    res->sz = (unsigned)(newsz - sizeof(*res));
    assert(res->sz == newsz - sizeof(*res));
    res->prod = 0;
    res->cons = res->prod;
    if (insert) {
        res->next = NULL;
        if (last)
            last->next = res;
        else
            first = res;
        last = res;
    }
    return res;
}

orerror<void>
buffer::receive(clientio io,
                fd_t fd,
                maybe<timestamp> deadline,
                maybe<uint64_t> limit)
{
    subbuf *b;
    bool unlinked;
    if (last && last->sz - last->prod >= 4096) {
        b = last;
        unlinked = false;
    } else {
        b = unlinked_subbuf(0);
        unlinked = true; }
    auto read(fd.read(io,
                      b->payload + b->prod,
                      min(b->sz - b->prod, limit.dflt(UINT64_MAX)),
                      deadline));
    if (read.isfailure()) {
        if (unlinked) free(b);
        return read.failure(); }
    if (unlinked) {
        b->next = NULL;
        if (last) last->next = b;
        else first = b;
        last = b; }
    assert(read.success() > 0);
    b->prod += read.success();
    prod += read.success();
    assert(b->cons != b->prod);
    return Success;
}

orerror<subscriptionbase *>
buffer::receive(clientio io,
                fd_t fd,
                subscriber &sub,
                maybe<timestamp> deadline) {
    {   iosubscription ios(sub, fd.poll(POLLIN));
        auto r(sub.wait(io, deadline));
        if (r == NULL) return error::timeout;
        if (r != &ios) return r; }
    auto r(receive(io, fd, deadline));
    if (r.isfailure()) return r.failure();
    else return NULL; }

orerror<void>
buffer::receivefast(fd_t fd) {
    /* Caller is supposed to have set O_NONBLOCK on the fd, so this
     * should be fast and doesn't need a clientio token. */
    return receive(clientio::CLIENTIO, fd, Nothing, Nothing); }

orerror<subscriptionbase *>
buffer::send(clientio io,
             fd_t fd,
             subscriber &sub,
             maybe<timestamp> deadline) {
    {   iosubscription ios(sub, fd.poll(POLLOUT));
        auto r(sub.wait(io, deadline));
        if (r == NULL) return error::timeout;
        if (r != &ios) return r; }
    assert(first);
    assert(first->prod != first->cons);
    auto wrote(fd.write(io,
                        first->payload + first->cons,
                        first->prod - first->cons,
                        deadline));
    if (wrote.isfailure())
        return wrote.failure();
    first->cons += wrote.success();
    if (first->cons == first->prod) {
        auto b = first;
        first = b->next;
        free(b);
        if (!first) last = NULL; }
    cons += wrote.success();
    return NULL; }

orerror<void>
buffer::sendfast(fd_t fd) {
    assert(first);
    assert(first->prod != first->cons);
    auto wrote(fd.writefast(first->payload + first->cons,
                            first->prod - first->cons));
    if (wrote.isfailure()) return wrote.failure();
    first->cons += wrote.success();
    cons += wrote.success();
    if (first->cons == first->prod) {
        auto b = first;
        first = b->next;
        free(b);
        if (first == NULL) last = NULL; }
    return Success; }

void
buffer::queue(const void *buf, size_t sz)
{
    if (sz == 0) return;
    auto tl(last);
    if (!tl || tl->sz - tl->prod < sz)
        tl = extend_end(sz);
    memcpy(tl->payload + tl->prod, buf, sz);
    tl->prod += sz;
    assert(tl->prod > tl->cons);
    prod += sz;
}

void
buffer::transfer(buffer &buf) {
    assert((buf.first == NULL) == (buf.last == NULL));
    assert((first == NULL) == (last == NULL));
    prod += buf.prod - buf.cons;
    if (first == NULL) first = buf.first;
    else last->next = buf.first;
    last = buf.last;
    buf.first = NULL;
    buf.last = NULL;
    buf.cons = buf.prod; }

bool
buffer::empty() const
{
    return first == NULL;
}

buffer::buffer(const buffer &o)
    : first(NULL),
      last(NULL),
      prod(o.cons),
      cons(o.cons) {
    if (o.prod == o.cons) return;
    extend_end(o.avail());
    auto cursor(o.first);
    while (cursor) {
        queue(cursor->payload + cursor->cons, cursor->prod - cursor->cons);
        cursor = cursor->next; } }

buffer::~buffer(void)
{
    auto i(first);
    while (i) {
        assert(i->cons < i->prod);
        auto n(i->next);
        free(i);
        i = n;
    }
}

size_t
buffer::avail() const
{
    return prod - cons;
}

void
buffer::fetch(void *buf, size_t sz)
{
    cons += sz;
    while (1) {
        if (sz == 0)
            return;
        /* Caller should check that there's enough stuff available before
           calling. */
        assert(first);
        assert(first->cons < first->prod);
        if (sz < first->prod - first->cons) {
            if (buf)
                memcpy(buf, first->payload + first->cons, sz);
            first->cons += sz;
            return;
        }

        if (buf) {
            memcpy(buf,
                   first->payload + first->cons,
                   first->prod - first->cons);
            buf = (void *)((unsigned long)buf + first->prod - first->cons);
        }
        sz -= first->prod - first->cons;
        auto n(first->next);
        free(first);
        first = n;
        if (!first) {
            assert(sz == 0);
            last = NULL; }
    }
}

size_t
buffer::offset() const
{
    return cons;
}

void
buffer::discard(size_t sz)
{
    fetch(NULL, sz);
}

buffer
buffer::steal() {
    buffer res;
    res.prod = prod;
    res.cons = cons;
    res.first = first;
    res.last = last;
    first = NULL;
    last = NULL;
    prod = cons;
    return res; }

unsigned char
buffer::idx(size_t off) const
{
    off -= cons;
    auto it(first);
    while (1) {
        assert(it);
        assert(it->cons < it->prod);
        if (off < it->prod - it->cons)
            return it->payload[it->cons + off];
        off -= it->prod - it->cons;
        it = it->next;
    }
}

/* This is logically const, because it doesn't change any
 * externally-visible state, but making it actually const would
 * involve either a lot of mutables or a lot of casts, so make it
 * non-const and provide a const wrapper (immediately after it). */
void *
buffer::linearise(size_t start, size_t end)
{
    subbuf *prev;
    assert(start >= cons);
    assert(end <= prod);
    assert(start <= end);
    if (start == end) {
        /* Caller asked for an empty buffer.  It doesn't matter what
           we give them, so just use a static buffer to make our lives
           a bit easier. */
        static unsigned char b;
        return &b; }
    start -= cons;
    end -= cons;
    prev = NULL;
    auto it(first);
    while (1) {
        assert(it);
        assert(it->cons < it->prod);
        if (start >= it->prod - it->cons) {
            start -= it->prod - it->cons;
            end -= it->prod - it->cons;
            prev = it;
            it = prev->next;
            continue;
        }
        if (end <= it->prod - it->cons)
            return it->payload + start + it->cons;
        break;
    }
    /* Going to have to actually linearise. */
    assert(it != last);
    assert(it->next != NULL);
    auto b(unlinked_subbuf(end - start));
    memcpy(b->payload,
           it->payload + it->cons + start,
           it->prod - it->cons - start);
    b->prod = it->prod - it->cons - start;
    assert(b->cons == 0);
    assert(b->sz >= end - start);
    if (start == 0) {
        /* Remove @it from list completely and replace it with b*/
        if (prev) {
            prev->next = b;
        } else {
            first = b; }
        b->next = it->next;
        free(it);
    } else {
        /* Trim the end of @it */
        it->prod = it->cons + start;
        auto n(it->next);
        b->next = n;
        it->next = b;
    }
    assert(b->next != first);
    prev = b;
    it = prev->next;
    end -= start;
    assert(it != first);
    while (b->prod < end) {
        assert(it != first);
        assert(it);
        if (it->prod - it->cons <= end - b->prod) {
            memcpy(b->payload + b->prod,
                   it->payload + it->cons,
                   it->prod - it->cons);
            b->prod += it->prod - it->cons;
            if (it == last) {
                last = prev; }
            prev->next = it->next;
            free(it);
            it = prev->next;
        } else {
            memcpy(b->payload + b->prod,
                   it->payload + it->cons,
                   end - b->prod);
            it->cons += end - b->prod;
            b->prod = end;
        }
    }
    if (!first) last = NULL;
    return b->payload;
}

/* linearise doesn't change any externally-visible state of the
   buffer, so allow it on const references. */
const void *
buffer::linearise(size_t start, size_t end) const {
    return const_cast<buffer *>(this)->linearise(start, end); }

bool
buffer::contenteq(const buffer &o) const {
    if (prod - cons != o.prod - o.cons) return false;
    if (prod == cons) return true;
    auto cursor1(first);
    auto cursor2(o.first);
    assert(cursor1);
    assert(cursor2);
    auto idx1(cursor1->cons);
    auto idx2(cursor2->cons);
    while (cursor1 && cursor2) {
        auto n(min(cursor1->prod - idx1, cursor2->prod - idx2));
        if (memcmp(cursor1->payload + idx1,
                   cursor2->payload + idx2,
                   n) != 0) {
            return false; }
        idx1 += n;
        idx2 += n;
        if (idx1 == cursor1->prod) {
            cursor1 = cursor1->next;
            if (cursor1) idx1 = cursor1->cons; }
        if (idx2 == cursor2->prod) {
            cursor2 = cursor2->next;
            if (cursor2) idx2 = cursor2->cons; } }
    assert(!cursor1);
    assert(!cursor2);
    return true; }

const bufferfield &
fields::mk(const buffer &b) {
    return bufferfield::mk(false, bufferfield::c_ascii, true, b); }

buffer::buffer(deserialise1 &ds)
    : first(NULL),
      last(NULL),
      prod(ds),
      cons(ds) {
    if (prod - cons > proto::maxmsgsize) ds.fail(error::invalidmessage);
    if (ds.isfailure()) {
        prod = 0;
        cons = 0;
        return; }
    if (prod == cons) return;
    auto s(unlinked_subbuf(prod - cons));
    s->prod = prod - cons;
    s->cons = 0;
    ds.bytes(s->payload, prod - cons);
    if (ds.isfailure()) {
        free(s);
        prod = 0;
        cons = 0;
        return; }
    s->next = NULL;
    first = s;
    last = s; }

void
buffer::serialise(serialise1 &s) const {
    s.push(prod);
    s.push(cons);
    for (auto it(first); it != NULL; it = it->next) {
        s.bytes(it->payload + it->cons, it->prod - it->cons); } }

const bufferfield &
bufferfield::mk(bool _showshape,
                enum contentform _content,
                bool _hiderepeats,
                const buffer &_b) {
    return *new bufferfield(_showshape, _content, _hiderepeats, _b); }

bufferfield::bufferfield(bool _showshape,
                         enum contentform _content,
                         bool _hiderepeats,
                         const buffer &_b)
    : showshape_(_showshape),
      content(_content),
      hiderepeats_(_hiderepeats),
      b(_b) {}

bufferfield::bufferfield(const bufferfield &o)
    : fields::field(o),
      showshape_(o.showshape_),
      content(o.content),
      hiderepeats_(o.hiderepeats_),
      b(o.b) {}

const bufferfield &
bufferfield::showshape() const {
    auto r = new bufferfield(*this);
    r->showshape_ = true;
    return *r; }

const bufferfield &
bufferfield::bytes() const {
    auto r = new bufferfield(*this);
    r->content = c_bytes;
    return *r; }

const bufferfield &
bufferfield::words() const {
    auto r = new bufferfield(*this);
    r->content = c_words;
    return *r; }

const bufferfield &
bufferfield::showrepeats() const {
    auto r = new bufferfield(*this);
    r->hiderepeats_ = false;
    return *r; }


void
bufferfield::fmt(fields::fieldbuf &buf) const {
    struct _ {
        const bufferfield &_this;
        fields::fieldbuf &_buf;
        bool isfirst;
        unsigned char repeatbyte;
        unsigned repeatcount;
        unsigned long repeatword;
        unsigned long wordacc;
        unsigned wordbytes;
        _(const bufferfield &__this, fields::fieldbuf &__buf)
            : _this(__this),
              _buf(__buf),
              isfirst(true),
              repeatbyte(0),
              repeatcount(0),
              repeatword(0),
              wordacc(0),
              wordbytes(0) {}
        void operator()(unsigned char byte) {
            switch (_this.content) {
            case c_ascii:
            case c_bytes:
                if (_this.hiderepeats_ && byte == repeatbyte) {
                    repeatcount++; }
                else {
                    flush();
                    repeatbyte = byte;
                    repeatcount = 1; }
                return;
            case c_words:
                wordacc = (wordacc << 8) | byte;
                wordbytes++;
                if (wordbytes == 8) {
                    wordbytes = 0;
                    if (wordacc == repeatword) {
                        repeatcount++; }
                    else {
                        flush();
                        repeatword = wordacc;
                        repeatcount = 1;
                        wordacc = 0; } }
                return; }
            if (!COVERAGE) abort(); }
        void flush() {
            if (repeatcount != 0) {
                bool suppressrepeats;
                switch (_this.content) {
                case c_ascii:
                    suppressrepeats = repeatcount >= 4;
                    break;
                case c_bytes:
                    suppressrepeats = repeatcount >= 2;
                    break;
                case c_words:
                    suppressrepeats = repeatcount >= 2;
                    break; }
                for (unsigned x = 0;
                     x < (suppressrepeats ? 1 : repeatcount);
                     x++) {
                    switch (_this.content) {
                    case c_ascii:
                        if (repeatbyte == '>' ||
                            repeatbyte == ']' ||
                            repeatbyte == '{' ||
                            repeatbyte == '\\') {
                            char _b[] = {'\\', (char)repeatbyte, 0};
                            _buf.push(_b); }
                        else if (isprint(repeatbyte)) {
                            char _b[] = {(char)repeatbyte, 0};
                            _buf.push(_b); }
                        else {
                            char _b[5];
                            sprintf(_b, "\\x%02x", repeatbyte);
                            _buf.push(_b); }
                        break;
                    case c_bytes: {
                        char _b[3];
                        sprintf(_b, "%s%02X", isfirst ? "" : ".", repeatbyte);
                        _buf.push(_b);
                        isfirst = false;
                        break; }
                    case c_words: {
                        if (!isfirst) _buf.push("; ");
                        fields::mk(be64toh(repeatword))
                            .base(16)
                            .uppercase()
                            .sep(fields::mk(":"), 4)
                            .hidebase()
                            .fmt(_buf);
                        isfirst = false;
                        break; } } }
                if (suppressrepeats) {
                    ("{" + fields::mk(repeatcount) + "}").fmt(_buf); }
                repeatcount = 0; }
            if (wordbytes != 0) {
                if (!isfirst) _buf.push("; ");
                fields::mk(be64toh(wordacc << (8 * (8 - wordbytes))))
                    .base(16)
                    .uppercase()
                    .sep(fields::mk(":"), 4)
                    .hidebase()
                    .fmt(_buf);
                if (wordbytes != 8) ("/" + fields::mk(wordbytes)).fmt(_buf);
                isfirst = false;
                wordbytes = 0;
                wordacc = 0; } }
        void reset() {
            flush();
            isfirst = true; }
    } iter(*this, buf);
    buf.push("<buffer: ");
    if (showshape_) {
        buf.push("prod:");
        fields::mk(b.prod).fmt(buf);
        buf.push(" cons:");
        fields::mk(b.cons).fmt(buf);
        buf.push(" ["); }
    for (auto it(b.first); it; it = it->next) {
        if (showshape_) {
            if (it != b.first) buf.push("...");
            buf.push("{prod:");
            fields::mk(it->prod).fmt(buf);
            buf.push(";cons:");
            fields::mk(it->cons).fmt(buf);
            buf.push(";sz:");
            fields::mk(it->sz).fmt(buf);
            buf.push("["); }
        for (auto x(it->cons); x != it->prod; x++) iter(it->payload[x]);
        if (showshape_) {
            iter.reset();
            buf.push("]}"); } }
    iter.flush();
    if (showshape_) buf.push("]");
    buf.push(">"); }
