/* byteswap.h uses a variable of storage class register, which isn't
 * allowed under clang with the warnings turned up.  Just #define it
 * away.  It's not like there are any good uses for the register
 * keyword. */
#define register

#include "buffer.H"

#include <string.h>
#include <unistd.h>

#include "either.H"
#include "fd.H"
#include "proto.H"
#include "pubsub.H"
#include "spark.H"
#include "test.H"
#include "timedelta.H"
#include "util.H"

#include "list.tmpl"
#include "spark.tmpl"
#include "timedelta.tmpl"
#include "wireproto.tmpl"

wireproto_wrapper_type(buffer::status_t)
void
buffer::status_t::addparam(
    wireproto::parameter<buffer::status_t> tmpl,
    wireproto::tx_message &out) const {
    out.addparam(
        wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
        wireproto::tx_compoundparameter()
        .addparam(proto::bufferstatus::prod, prod)
        .addparam(proto::bufferstatus::cons, cons)); }
maybe<buffer::status_t>
buffer::status_t::fromcompound(const wireproto::rx_message &msg) {
#define doparam(name) auto name(msg.getparam(proto::bufferstatus::name))
    doparam(prod);
    doparam(cons);
#undef doparam
    if (!prod || !cons) return Nothing;
    else return buffer::status_t(prod.just(), cons.just()); }
const fields::field &
fields::mk(const buffer::status_t &o) {
    return "<prod:" + mk(o.prod) +
        " cons:" + mk(o.cons) +
        ">"; }
bufferstatus::bufferstatus(quickcheck q) {
    do {
        prod = q;
        cons = q;
    } while (prod > cons); }
bool
bufferstatus::operator==(const bufferstatus &o) const {
    return prod == o.prod && cons == o.cons; }

buffer::subbuf *
buffer::newsubbuf(size_t sz, bool middle, bool atstart, bool insert)
{
    subbuf *res;
    size_t newsz;
    for (newsz = 16384; newsz < sz + sizeof(*res); newsz *= 2)
        ;
    res = (subbuf *)malloc(newsz);
    new (res) subbuf();
    res->sz = (unsigned)(newsz - sizeof(*res));
    assert(res->sz == newsz - sizeof(*res));
    if (middle)
        res->prod = (res->sz + sz) / 2;
    else
        res->prod = 0;
    res->cons = res->prod;
    if (insert) {
        if (atstart) {
            res->next = first;
            first = res;
            if (!last)
                last = res;
        } else {
            res->next = NULL;
            if (last)
                last->next = res;
            else
                first = res;
            last = res;
        }
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
    if (read.success() == 0) return error::pastend;
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

void
buffer::pushback(const void *what, size_t sz)
{
    assert(cons >= sz);
    if (!sz) return;
    auto b(first);
    assert(!b || b->cons < b->prod);
    if (!(b && b->cons >= sz)) {
        b = extend_start(sz);
        assert(b->cons <= b->sz);
    }
    assert(b);
    assert(b->cons <= b->sz);
    assert(b->cons >= sz);
    b->cons -= sz;
    memcpy((void *)((unsigned long)b->payload + b->cons), what, sz);
    cons -= sz;
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

buffer::status_t
buffer::status() const {
    return status_t(prod, cons); }

const bufferfield &
fields::mk(const buffer &b) {
    return bufferfield::mk(false, bufferfield::c_ascii, true, b); }

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

void
tests::buffer(void)
{
    testcaseS(
        "buffer",
        "fuzz",
        [] (support &t) {
            static const int nr_iters = 10;
            for (int i = 0; i < nr_iters; i++) {
                t.msg("iteration %d/%d", i, nr_iters);
                auto b = new ::buffer();

                unsigned char *content;
                size_t size;
                unsigned long prod;
                unsigned long cons;

                int cntr;

                content = NULL;
                size = 0;
                prod = 0;
                cons = 0;
                cntr = 0;

                for (int j = 0; j < 1000; j++) {
                    assert(b->empty() == (size == 0));
                    assert(b->avail() == size);
                    assert(b->offset() == (unsigned long)cons);
                    assert(size == (size_t)(prod - cons));
                    switch (random() % 6) {
                    case 0: {
                        size_t sz = random() % 65536;
                        t.detail(" queue %zd", sz);
                        content = (unsigned char *)realloc(content, size + sz);
                        for (unsigned k = 0; k < sz; k++)
                            content[k + size] = (unsigned char)cntr++;
                        b->queue(content + size, sz);
                        size += sz;
                        prod += sz;
                        break;
                    }
                    case 1: {
                        if (!size)
                            continue;
                        size_t sz = (unsigned long)random() % size;
                        t.detail(" fetch %zd", sz);
                        unsigned char *buffer = (unsigned char *)malloc(sz);
                        b->fetch(buffer, sz);
                        cons += sz;
                        for (unsigned k = 0; k < sz; k++)
                            assert( buffer[k] == content[k] );
                        memmove(content, content + sz, size - sz);
                        size -= sz;
                        free(buffer);
                        break;
                    }
                    case 2: {
                        if (!cons)
                            continue;
                        size_t sz = (unsigned long)random() % cons;
                        t.detail(" pushback %zd", sz);
                        content = (unsigned char *)realloc(content, size + sz);
                        memmove(content + sz, content, size);
                        for (unsigned k = 0; k < sz; k++)
                            content[k] = (unsigned char)cntr++;
                        b->pushback(content, sz);
                        cons -= sz;
                        size += sz;
                        break;
                    }
                    case 3: {
                        if (!size)
                            continue;
                        size_t sz = (unsigned long)random() % size;
                        t.detail(" discard %zd", sz);
                        b->discard(sz);
                        memmove(content, content + sz, size - sz);
                        cons += sz;
                        size -= sz;
                        break;
                    }
                    case 4: {
                        if (size <= 1)
                            continue;
                        size_t start = cons +
                            ((unsigned long)random() % (size - 1));
                        size_t end = start +
                            ((unsigned long)random() % (prod - start));
                        t.detail(" linearise %zd %zd", start, end);
                        const unsigned char *buf =
                            (const unsigned char *)b->linearise(start, end);
                        assert(buf);
                        for (unsigned k = 0; k < end - start; k++)
                            assert( buf[k] == content[k+start-cons]);
                        break;
                    }
                    case 5: {
                        if (!size)
                            continue;
                        size_t off = cons + ((unsigned long)random() % size);
                        t.detail(" idx %zd", off);
                        assert(b->idx(off) == content[off - cons]);
                        break;
                    }
                    }
                }
                free(content);
                delete b;
            }
        });

    /* A couple more test cases to get to 100% coverage */
    testcaseV("buffer", "status", [] () {
            ::buffer buf;
            buf.queue("Hello", 5);
            char b[3];
            buf.fetch(b, 3);
            auto s(buf.status());
            assert(s.prod == 5);
            assert(s.cons == 3);
            fields::fieldbuf fb;
            fields::mk(s).fmt(fb);
            assert(!strcmp(fb.c_str(), "<prod:5 cons:3>")); } );

    testcaseV("buffer", "statuswire", [] () {
            ::buffer buf1;
            buf1.queue("Hello", 5);
            char b[3];
            buf1.fetch(b, 3);
            auto s(buf1.status());
            wireproto::parameter<buffer::status_t> param(7);
            wireproto::msgtag tag(99);
            {   ::buffer buf2;
                wireproto::tx_message(tag)
                    .serialise(buf2, wireproto::sequencenr::invalid);
                auto r(wireproto::rx_message::fetch(buf2));
                assert(r.issuccess());
                assert(r.success().getparam(param) == Nothing); }
            {   ::buffer buf2;
                wireproto::tx_message(tag).addparam(param,s)
                    .serialise(buf2, wireproto::sequencenr::invalid);
                auto r(wireproto::rx_message::fetch(buf2));
                assert(r.issuccess());
                assert(r.success().getparam(param).isjust());
                assert(r.success().getparam(param).just().cons == s.cons);
                assert(r.success().getparam(param).just().prod == s.prod); }
            wireproto::roundtrip<buffer::status_t>();});

    testcaseV("buffer", "pushbackempty", [] () {
            ::buffer buf;
            buf.queue("Hello", 5);
            char b[5];
            buf.fetch(b, 5);
            buf.pushback(b, 5);
            char b2[5];
            buf.fetch(b2, 5);
            assert(!memcmp(b2, "Hello", 5)); });

    testcaseIO("buffer", "send", [] (clientio io) {
            auto pipe(fd_t::pipe());
            assert(pipe.issuccess());
            ::buffer buf;
            buf.queue("ABC", 3);
            {   subscriber sub;
                auto res(buf.send(io,
                                  pipe.success().write,
                                  sub,
                                  Nothing));
                assert(res.issuccess());
                assert(res.success() == NULL); }
            assert(buf.empty());
            buf.queue("D", 1);
            {   subscriber sub;
                auto res(buf.send(io,
                                  pipe.success().write,
                                  sub,
                                  Nothing));
                assert(res.issuccess());
                assert(res.success() == NULL); }
            assert(buf.empty());
            char bb[4];
            auto res(pipe.success().read.read(io,
                                              bb,
                                              4,
                                              Nothing));
            assert(res.issuccess());
            assert(res.success() == 4);
            assert(!memcmp(bb, "ABCD", 4));
            pipe.success().close(); });

    testcaseIO("buffer", "senderror", [] (clientio io) {
            auto pipe(fd_t::pipe());
            pipe.success().close();
            ::buffer buf;
            buf.queue("HELLO", 5);
            subscriber sub;
            auto r(buf.send(io, pipe.success().write, sub));
            assert(r.isfailure());
            assert(r.failure() == error::from_errno(EBADF)); });

    testcaseIO("buffer", "receive", [] (clientio io) {
            auto pipe(fd_t::pipe());
            assert(pipe.issuccess());
            {   auto res(pipe.success().write.write(io,
                                                    "GHI",
                                                    3));
                assert(res.issuccess());
                assert(res.success() == 3); }
            ::buffer buf;
            {   auto res(buf.receive(io, pipe.success().read));
                assert(res.issuccess());
                assert(buf.avail() == 3);
                char bb[2];
                buf.fetch(bb, 2);
                assert(!memcmp(bb, "GH", 2)); }
            {   auto res(pipe.success().write.write(io,
                                                    "JKLM",
                                                    4));
                assert(res.issuccess());
                assert(res.success() == 4); }
            {   auto res(buf.receive(io, pipe.success().read));
                assert(res.issuccess());
                assert(buf.avail() == 5);
                char bb[5];
                buf.fetch(bb, 5);
                assert(!memcmp(bb, "IJKLM", 5)); }
            pipe.success().close(); });

    testcaseIO("buffer", "receivetimeout", [] (clientio io) {
            auto pipe(fd_t::pipe());
            assert(pipe.issuccess());
            ::buffer buf;
            {   auto res(buf.receive(
                             io,
                             pipe.success().read,
                             timestamp::now() + timedelta::milliseconds(10)));
                assert(res.isfailure());
                assert(res.failure() == error::timeout); }
            {   auto res(pipe.success().write.write(io,
                                                    "JKLM",
                                                    4));
                assert(res.issuccess());
                assert(res.success() == 4); }
            {   subscriber sub;
                auto res(buf.receive(io,
                                     pipe.success().read,
                                     sub));
                assert(res.issuccess());
                assert(res.success() == NULL);
                assert(buf.avail() == 4);
                char bb[4];
                buf.fetch(bb, 4);
                assert(!memcmp(bb, "JKLM", 4)); }
            pipe.success().close(); });

    testcaseV("buffer", "linearisestartofbuf", [] () {
            ::buffer buf;
            char b[16384];
            memset(b, 'Z', sizeof(b));
            for (int i = 0; i < 32; i++) buf.queue(b, sizeof(b));
            char *z = (char *)buf.linearise(0, sizeof(b) * 32);
            for (unsigned i = 0; i < sizeof(b) * 32; i++) {
                assert(z[i] == 'Z'); } });

    testcaseV("buffer", "linearisemultiplebuf", [] () {
            ::buffer buf;
            char b[16389];
            memset(b, 'Z', sizeof(b));
            for (int i = 0; i < 32; i++) buf.queue(b, sizeof(b));
            char *z = (char *)buf.linearise(sizeof(b), sizeof(b) * 31);
            for (unsigned i = 0; i < sizeof(b) * 30; i++) {
                assert(z[i] == 'Z'); } });

    testcaseIO("buffer", "receivenotify", [] (clientio io) {
            ::buffer buf;
            auto pipe(fd_t::pipe());
            subscriber sub;
            publisher pub;
            subscription scn(sub, pub);
            spark<bool> worker([&pub] () {
                    sleep(1);
                    pub.publish();
                    return true;});
            auto res(buf.receive(io,
                                 pipe.success().read,
                                 sub));
            assert(res.issuccess());
            assert(res.success() == &scn);
            assert(worker.get() == true);
            pipe.success().close(); });

    testcaseIO("buffer", "receivefailure", [] (clientio io) {
            ::buffer buf;
            auto pipe(fd_t::pipe());
            pipe.success().close();
            auto t(buf.receive(io, pipe.success().read));
            assert(t.isfailure());
            assert(t == error::from_errno(EBADF));
            assert(buf.empty()); });

    testcaseV("buffer", "queueempty", [] () {
            ::buffer buf;
            buf.queue("", 0);
            assert(buf.avail() == 0);;
            buf.queue("X", 1);
            assert(buf.avail() == 1); });

    testcaseV("buffer", "clone", [] () {
            {   ::buffer buf1;
                ::buffer buf2(buf1);
                assert(buf2.avail() == 0); }
            for (int i = 0; i < 100; i++) {
                ::buffer buf1;
                for (int j = 0; j < 10; j++) {
                    switch (random() % 2) {
                    case 0: {
                        size_t sz(random() % 65536);
                        unsigned char b[sz];
                        memset(b, j, sz);
                        buf1.queue(b, sz); }
                    case 1: {
                        if (buf1.avail() == 0) continue;
                        size_t sz((unsigned long)random() % buf1.avail());
                        unsigned char b[sz];
                        buf1.fetch(b, sz); } } }
                ::buffer buf2(buf1);
                assert(buf1.offset() == buf2.offset());
                assert(buf1.avail() == buf2.avail());
                assert(buf1.contenteq(buf2));
                {   ::buffer buf3(buf1);
                    assert(buf1.offset() == buf3.offset());
                    assert(buf1.avail() == buf3.avail());
                    assert(buf1.contenteq(buf3)); }
                assert(buf1.offset() == buf2.offset());
                assert(buf1.avail() == buf2.avail());
                assert(buf1.contenteq(buf2)); } });

    testcaseV("buffer", "steal", [] () {
            ::buffer buf1;
            buf1.queue("HELLO", 5);
            char b;
            buf1.fetch(&b, 1);
            ::buffer buf2(buf1.steal());
            assert(buf2.avail() == 4);
            assert(buf2.offset() == 1);
            assert(buf1.avail() == 0);
            assert(buf1.offset() == 1);
            assert(memcmp(buf2.linearise(1, 5), "ELLO", 4) == 0); });

    testcaseV("buffer", "constlinearise", [] () {
            ::buffer buf1;
            buf1.queue("HELLO", 5);
            const ::buffer &ref(buf1);
            assert(memcmp(ref.linearise(0, 5), "HELLO", 5) == 0); });

    testcaseV("buffer", "buffercmp", [] () {
            {   ::buffer buf1;
                ::buffer buf2;
                assert(buf1.contenteq(buf2));
                buf1.queue("HELLO", 5);
                assert(!buf1.contenteq(buf2));
                buf2.queue("H", 1);
                buf2.queue("E", 1);
                buf2.queue("L", 1);
                buf2.queue("L", 1);
                buf2.queue("O", 1);
                assert(buf1.contenteq(buf2));
                buf1.queue("A", 1);
                buf2.queue("B", 1);
                assert(!buf1.contenteq(buf2)); }
            {   ::buffer buf1;
                ::buffer buf2;
                buf1.queue("ABCD", 4);
                buf2.queue("CD", 2);
                assert(!buf1.contenteq(buf2));
                char b[2];
                buf1.fetch(b, 2);
                assert(buf1.contenteq(buf2)); } });

    testcaseV("buffer", "bufferfield", [] () {
            {   ::buffer buf;
                assert(strcmp(fields::mk(buf).c_str(), "<buffer: >") == 0); }
            {   ::buffer buf;
                assert(strcmp(fields::mk(buf).showshape().c_str(),
                              "<buffer: prod:0 cons:0 []>") == 0); }
            {   ::buffer buf;
                buf.queue("AAAAAAAAAAAAAAAA", 16);
                assert(strcmp(fields::mk(buf).c_str(),
                              "<buffer: A{16}>") == 0); }
            {   ::buffer buf;
                buf.queue("ABCDEFGHIJKLMNOP", 16);
                assert(strcmp(fields::mk(buf).c_str(),
                              "<buffer: ABCDEFGHIJKLMNOP>") == 0); }
            {   ::buffer buf;
                buf.queue("AAABBBCCCDDDEEE", 15);
                assert(strcmp(fields::mk(buf).c_str(),
                              "<buffer: AAABBBCCCDDDEEE>") == 0); }
            {   ::buffer buf;
                buf.queue("AAAABBBBCCCCDDDD", 16);
                assert(strcmp(fields::mk(buf).c_str(),
                              "<buffer: A{4}B{4}C{4}D{4}>") == 0); }
            {   ::buffer buf;
                buf.queue("AAAABBBBBBCCCDDDDE", 18);
                assert(strcmp(fields::mk(buf).c_str(),
                              "<buffer: A{4}B{6}CCCD{4}E>") == 0); }
            {   ::buffer buf;
                buf.queue("AAAABBBBCCCCDDDDE", 17);
                assert(strcmp(fields::mk(buf).showrepeats().c_str(),
                              "<buffer: AAAABBBBCCCCDDDDE>") == 0); }
            {   ::buffer buf;
                buf.queue("\\{]>\x5", 5);
                assert(strcmp(fields::mk(buf).c_str(),
                              "<buffer: \\\\\\{\\]\\>\\x05>") == 0); }
            {   ::buffer buf;
                assert(strcmp(fields::mk(buf).bytes().c_str(),
                              "<buffer: >") == 0); }
            {   ::buffer buf;
                buf.queue("\x1\x2\x3", 3);
                assert(strcmp(fields::mk(buf).bytes().c_str(),
                              "<buffer: 01.02.03>") == 0); }
            {   ::buffer buf;
                buf.queue("\x1\x1\x1", 3);
                assert(strcmp(fields::mk(buf).bytes().c_str(),
                              "<buffer: 01{3}>") == 0); }
            {   ::buffer buf;
                assert(strcmp(fields::mk(buf).words().c_str(),
                              "<buffer: >") == 0); }
            {   ::buffer buf;
                buf.queue("\x1", 1);
                assert(strcmp(fields::mk(buf).words().c_str(),
                              "<buffer: 1/1>") == 0); }
            {   ::buffer buf;
                buf.queue("\x1\x1\x1\x1\x1\x1\x1\x1", 8);
                assert(strcmp(fields::mk(buf).words().c_str(),
                              "<buffer: 101:0101:0101:0101>") == 0); }
            {   ::buffer buf;
                buf.queue("\x1\x1\x1\x1\x1\x1\x1\x1\x1\x1\x1", 11);
                assert(strcmp(fields::mk(buf).words().c_str(),
                              "<buffer: 101:0101:0101:0101; 1:0101/3>")
                       == 0); }
            {   ::buffer buf;
                buf.queue("\x1\x2\x3\x4\x5\x6\x7\x8\x9\xa\xb", 11);
                assert(strcmp(fields::mk(buf).words().c_str(),
                              "<buffer: 807:0605:0403:0201; B:0A09/3>")
                       == 0); }
            {   ::buffer buf;
                buf.queue("\x1\x1\x1\x1\x1\x1\x1\x1", 8);
                buf.queue("\x1\x1\x1\x1\x1\x1\x1\x1", 8);
                assert(strcmp(fields::mk(buf).words().c_str(),
                              "<buffer: 101:0101:0101:0101{2}>") == 0); }
            {   ::buffer buf;
                buf.queue("HELLO", 5);
                char b;
                buf.fetch(&b, 1);
                assert(strcmp(
                           fields::mk(buf).showshape().c_str(),
                           "<buffer: prod:5 cons:1 [{prod:5;cons:1;sz:16,352[ELLO]}]>")
                       == 0); } });
    testcaseV("buffer", "transfer", [] {
            ::buffer buf1;
            buf1.queue("HELLO", 5);
            ::buffer buf2;
            buf2.transfer(buf1);
            assert(buf1.empty());
            assert(buf2.avail() == 5);
            char bytes[11];
            buf2.fetch(bytes, 3);
            assert(buf2.avail() == 2);
            assert(!memcmp(bytes, "HEL", 3));
            buf1.queue("GOODBYE", 7);
            buf2.transfer(buf1);
            assert(buf2.avail() == 9);
            buf2.fetch(bytes, 9);
            assert(!memcmp(bytes, "LOGOODBYE", 9)); });
    testcaseV("buffer", "fastio", [] {
            auto p(fd_t::pipe().fatal("pipe"));
            p.read.nonblock(true).fatal("nonblock read");
            p.write.nonblock(true).fatal("nonblock write");
            ::buffer buf1;
            assert(buf1.receivefast(p.read) == error::wouldblock);
            assert(buf1.empty());
            ::buffer buf2;
            buf2.queue("HELLO", 5);
            buf2.sendfast(p.write).fatal("sending fast");
            assert(buf2.empty());
            buf1.receivefast(p.read).fatal("receiving fast");
            assert(buf1.avail() == 5);
            assert(memcmp(buf1.linearise(0, 5), "HELLO", 5) == 0);
            size_t sent = 0;
            while (true) {
                auto a(buf2.avail());
                assert(a < 5);
                buf2.queue("HELLO", 5);
                auto r(timedelta::time<orerror<void> >([&buf2, &p] {
                            return buf2.sendfast(p.write); }));
                assert(r.td < timedelta::milliseconds(10));
                if (r.v == error::wouldblock) break;
                r.v.fatal("sending fast");
                assert(buf2.avail() < 5 + a);
                sent += 5 + a - buf2.avail(); }
            assert(sent >= 8192); });
}
