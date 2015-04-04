#include "buffer.H"

#include <limits.h>

#include "clientio.H"
#include "fd.H"
#include "proto2.H"
#include "pubsub.H"
#include "serialise.H"
#include "util.H"

#define DEFAULT_BUF_SIZE 16384u
#define SMALL_SYSCALL 4096u

/* Move the contents of a subbuffer to the start of its payload
 * area. */
void
buffer::subbuf::squashstartslack() {
    assert(startoff <= endoff);
    memmove(_payload, _payload + startslack, size());
    startoff += startslack;
    endoff += startslack;
    endslack += startslack;
    startslack = 0; }

buffer::subbuf *
buffer::subbuf::fresh(size_t start, size_t minsize) {
    assert(minsize < UINT_MAX);
    if (minsize < DEFAULT_BUF_SIZE) minsize = DEFAULT_BUF_SIZE;
    auto res = (subbuf *)malloc(minsize);
    res->startoff = start;
    res->startslack = 0;
    res->endslack = (unsigned)(minsize - sizeof(*res));
    res->endoff = start + res->endslack;
    res->next = NULL;
    return res; }

buffer::buffer(const buffer &o)
    : first(NULL),
      last(NULL),
      mru(NULL) {
    first = subbuf::fresh(o.offset(), DEFAULT_BUF_SIZE);
    last = first;
    mru = first;
    for (auto it(o.first); it != NULL; it = it->next) {
        queue(it->payload(it->start()), it->size()); } }

buffer::buffer()
    : first(NULL),
      last(NULL),
      mru(NULL) {
    first = subbuf::fresh(0, DEFAULT_BUF_SIZE);
    last = first;
    mru = first; }

orerror<void>
buffer::receive(clientio io,
                fd_t fd,
                maybe<timestamp> deadline,
                maybe<uint64_t> limit) {
    /* We always try to get at least 4k in the buffer we pass to the
     * kernel. */
    if (last->endslack >= SMALL_SYSCALL) { /* Already plenty of space */ }
    else if (last->startslack + last->endslack >= SMALL_SYSCALL * 2) {
        /* Not enough space at the end, but there would be plenty if
         * we shuffled things to the beginning of the buffer. */
        last->squashstartslack(); }
    else {
        /* Need a new buffer. */
        auto b = subbuf::fresh(last->end(), DEFAULT_BUF_SIZE);
        last->next = b;
        last = b;
        mru = last; }
    auto tocopy(min(last->endslack,
                    (uint32_t)min(limit.dflt(UINT64_MAX), UINT32_MAX)));
    auto read(fd.read(io,
                      last->payload(last->end()),
                      tocopy,
                      deadline));
    if (read.isfailure()) return read.failure();
    assert(read.success() > 0);
    assert(read.success() <= last->endslack);
    last->endslack -= (unsigned)read.success();
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
    iosubscription ios(sub, fd.poll(POLLOUT));
    auto s(sub.wait(io, deadline));
    if (s == NULL) return error::timeout;
    if (s != &ios) return s;
    auto r(sendfast(fd));
    if (r.isfailure()) return r.failure();
    else return NULL; }

orerror<void>
buffer::sendfast(fd_t fd) {
    /* Skip empty sub-buffers at the start. */
    while (first->size() == 0) {
        auto n(first->next);
        free(first);
        if (mru == first) mru = n;
        first = n; }
    /* Avoid sending very small things over the wire for no good
     * reason. */
    while (first->size() < SMALL_SYSCALL &&
           first->next != NULL) {
        if (first->endslack < SMALL_SYSCALL) {
            assert(first->startslack >= SMALL_SYSCALL);
            first->squashstartslack(); }
        auto to_copy(min(first->endslack, first->next->size()));
        memcpy(first->payload(first->end()),
               first->next->payload(first->next->start()),
               to_copy);
        first->endslack -= to_copy;
        first->next->startslack += to_copy;
        if (first->next->size() == 0) {
            auto b(first->next);
            first->next = b->next;
            free(b);
            if (last == b) last = first;
            if (mru == b) mru = last; } }
    auto wrote(fd.writefast(first->payload(first->start()), first->size()));
    if (wrote.isfailure()) return wrote.failure();
    assert(wrote.success() <= first->size());
    first->startslack += (unsigned)wrote.success();
    return Success; }

void
buffer::queue(const void *buf, size_t sz) {
    if (last->endslack < sz && last->startslack + last->endslack >= sz) {
        last->squashstartslack(); }
    if (last->endslack < sz) {
        auto bufsz(DEFAULT_BUF_SIZE);
        while (bufsz < sz) bufsz *= 2;
        auto b(subbuf::fresh(last->end(), bufsz + sizeof(subbuf)));
        last->next = b;
        last = b; }
    memcpy(last->payload(last->end()), buf, sz);
    last->endslack -= (unsigned)sz; }

void
buffer::transfer(buffer &buf) {
    auto mark(buf.last->endoff);
    for (auto b(buf.first); b != NULL; b = b->next) {
        assert(b->startoff <= b->endoff);
        auto delta(last->end() - b->start());
        b->startoff += delta;
        b->endoff += delta;
        assert(b->startoff <= b->endoff);
        assert(b->start() == last->end());
        last->next = b;
        last = b; }
    auto b(subbuf::fresh(mark, DEFAULT_BUF_SIZE));
    buf.first = b;
    buf.last = b; }

bool
buffer::empty() const {
    for (auto it(first); it != NULL; it = it->next) {
        if (it->size() != 0) return false; }
    return true; }

buffer::~buffer(void) {
    while (first != NULL) {
        auto n(first->next);
        free(first);
        first = n; } }

size_t
buffer::avail() const { return last->end() - first->start(); }

void
buffer::fetch(void *buf, size_t sz) {
    size_t start(first->start());
    size_t end(start + sz);
    size_t cursor(start);
    assert(end <= last->end());
    while (cursor < end) {
        if (first->next != NULL) assert(first->end() == first->next->start());
        assert(first->start() < end);
        auto to_copy = min(end, first->end()) - cursor;
        if (buf != NULL) {
            memcpy((void *)((uintptr_t)buf + cursor - start),
                   first->payload(cursor),
                   to_copy); }
        cursor += to_copy;
        first->startslack += (unsigned)to_copy;
        if (first->size() == 0 && first != last) {
            auto n(first->next);
            free(first);
            if (mru == first) mru = n;
            first = n; } } }

size_t
buffer::offset() const { return first->start(); }

void
buffer::discard(size_t sz) { fetch(NULL, sz); }

buffer::buffer(_Steal, buffer &o)
    : first(o.first),
      last(o.last),
      mru(o.mru) {
    o.first = subbuf::fresh(o.offset(), DEFAULT_BUF_SIZE);
    o.last = o.first;
    o.mru = o.first; }

unsigned char
buffer::idx(size_t off) const {
    for (auto it(first); true; it = it->next) {
        assert(off >= it->start());
        if (off < it->end()) return *(unsigned char *)it->payload(off); } }

/* This is logically const, because it doesn't change any
 * externally-visible state, but making it actually const would
 * involve either a lot of mutables or a lot of casts, so make it
 * non-const and provide a const wrapper (immediately after it). */
void *
buffer::linearise(size_t start, size_t end) {
    assert(start >= first->start());
    assert(end <= last->end());
    if (end == start) {
        /* Caller asked for an empty buffer.  It doesn't matter what
           we give them, so just use a static buffer to make our lives
           a bit easier. */
        static unsigned char b;
        return &b; }
    subbuf *it(NULL);
    /* Skip the bits which are unambiguously too soon. */
    if (mru->start() <= start) it = mru;
    else if (last->start() <= start) it = last;
    else it = first;
    while (it->end() <= start) it = it->next;
    mru = it;
    /* @it contains at least some of the data we need.  If it contains
     * all of it then we're done. */
    if (it->end() >= end) return it->payload(start);
    /* Otherwise, we're going to need to restructure things. */
    /* If there's enough space in @it then we'll pull from the next
     * buffer into there. */
    if (it->endoff < end && it->endoff + it->startslack >= end) {
        it->squashstartslack(); }
    if (it->endoff >= end) {
        while (it->end() < end) {
            auto n(it->next);
            auto tocopy(min(it->endslack, n->size()));
            memmove(it->payload(it->end()), n->payload(n->start()), tocopy);
            n->startslack += tocopy;
            it->endslack -= tocopy;
            if (n->size() == 0) {
                it->next = n->next;
                free(n);
                if (mru == n) mru = it;
                if (it->next == NULL) last = it; } }
        return it->payload(start); }
    /* Going to need a new buffer. */
    auto b(subbuf::fresh(start, end - start + sizeof(subbuf)));
    assert(b->startslack == 0);
    assert(b->start() == start);
    assert(b->endoff >= end);
    auto tocopy(it->end() - start);
    assert(b->endslack >= tocopy);
    memcpy(b->payload(start), it->payload(start), tocopy);
    b->endslack -= (unsigned)tocopy;
    it->endslack += (unsigned)tocopy;
    assert(it->end() == start);
    auto n(it->next);
    it->next = b;
    it = n;
    while (b->end() != end) {
        assert(b->endslack > 0);
        assert(b->endoff >= end);
        assert(b->end() < end);
        tocopy = min(end - b->end(), it->size());
        assert(b->endslack >= tocopy);
        memcpy(b->payload(b->end()), it->payload(it->start()), tocopy);
        b->endslack -= (unsigned)tocopy;
        it->startslack += (unsigned)tocopy;
        if (it->end() == it->start()) {
            n = it->next;
            free(it);
            if (it == mru && n == NULL) mru = first;
            if (it == mru) mru = n;
            if (it == last) assert(n == NULL);
            it = n; } }
    b->next = it;
    if (b->next == NULL) last = b;
    if (n != NULL) assert(n->start() == b->end());
    mru = b;
    return b->payload(start); }

/* linearise doesn't change any externally-visible state of the
   buffer, so allow it on const references. */
const void *
buffer::linearise(size_t start, size_t end) const {
    return const_cast<buffer *>(this)->linearise(start, end); }

bool
buffer::contenteq(const buffer &o) const {
    auto delta(first->start() - o.first->start());
    auto cursor1(first);
    auto cursor2(o.first);
    while (true) {
        while (cursor1 != NULL && cursor1->start() == cursor1->end()) {
            cursor1 = cursor1->next; }
        while (cursor2 != NULL && cursor2->start() == cursor2->end()) {
            cursor2 = cursor2->next; }
        if (cursor1 == NULL || cursor2 == NULL) break;
        auto startcompare(max(cursor1->start(), cursor2->start() + delta));
        auto endcompare(min(cursor1->end(), cursor2->end() + delta));
        if (memcmp(cursor1->payload(startcompare),
                   cursor2->payload(startcompare - delta),
                   endcompare - startcompare)) {
            return false; }
        if (endcompare == cursor1->end()) cursor1 = cursor1->next;
        if (endcompare == cursor2->end() + delta) cursor2 = cursor2->next; }
    return cursor1 == NULL && cursor2 == NULL; }

const bufferfield &
fields::mk(const buffer &b) {
    return bufferfield::mk(false, bufferfield::c_ascii, true, b); }

buffer::buffer(deserialise1 &ds)
    : first(NULL),
      last(NULL),
      mru(NULL) {
    size_t start(ds);
    size_t end(ds);
    if (end < start) ds.fail(error::invalidmessage);
    if (end > start + proto::maxmsgsize) ds.fail(error::overflowed);
    if (ds.isfailure()) {
        first = subbuf::fresh(0, DEFAULT_BUF_SIZE);
        last = first;
        return; }
    auto s(subbuf::fresh(start, end - start + sizeof(subbuf)));
    assert(s->endslack >= end - start);
    assert(s->start() == start);
    assert(s->end() == start);
    ds.bytes(s->payload(s->end()), end - start);
    s->endslack -= (unsigned)(end - start);
    assert(s->end() == end);
    first = s;
    last = s;
    mru = s; }

void
buffer::serialise(serialise1 &s) const {
    s.push(first->start());
    s.push(last->end());
    for (auto it(first); it != NULL; it = it->next) {
        s.bytes(it->payload(it->start()), it->size()); } }

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
    if (showshape_) buf.push("[");
    for (auto it(b.first); it; it = it->next) {
        if (showshape_) {
            if (it != b.first) buf.push("...");
            if (it == b.mru) buf.push("!");
            buf.push("<");
            fields::mk(it->startoff).base(16).hidebase().nosep().fmt(buf);
            buf.push("+");
            fields::mk(it->startslack).base(16).hidebase().nosep().fmt(buf);
            buf.push(":");
            fields::mk(it->endoff).base(16).hidebase().nosep().fmt(buf);
            buf.push("-");
            fields::mk(it->endslack).base(16).hidebase().nosep().fmt(buf);
            if (it->size() != 0) buf.push(" "); }
        for (auto x(it->start()); x != it->end(); x++) {
            iter(*(unsigned char *)it->payload(x)); }
        if (showshape_) {
            iter.reset();
            buf.push(">"); } }
    iter.flush();
    if (showshape_) buf.push("]");
    buf.push(">"); }
