#include "fields.H"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "list.H"
#include "list.tmpl"
#include "test.H"

#include "fieldfinal.H"

namespace fields {

struct arena {
    arena *next;
    unsigned size;
    unsigned used;
    unsigned char content[];
};

static struct : public field {
    void fmt(fieldbuf &p) const { p.push(" "); }
} _space;
const field &space(_space);

static __thread arena *arenas;
static __thread fieldbuf *buffers;

static void *
_alloc(size_t sz)
{
    /* Keep everything 16-byte aligned, for general sanity. */
    sz = (sz + 15) & ~15ul;
    if (!arenas || arenas->used + sz > arenas->size) {
        size_t size;
        for (size = 8192;
             size - 32 - sizeof(arena) < sz;
             size *= 4)
            ;
        arena *a = (arena *)malloc(size - 32);
        a->next = arenas;
        a->size = size - 32 - sizeof(*a);
        a->used = 0;
        memset(a->content, 'Z', a->size);
        arenas = a;
    }
    void *res = &arenas->content[arenas->used];
    arenas->used += sz;
    return res;
}

template <typename t> static t *
alloc()
{
    return (t *)_alloc(sizeof(t));
}

template <typename t> static t
min(t a, t b)
{
    if (a < b)
        return a;
    else
        return b;
}

fieldbuf::fieldbuf()
    : owner(tid::me()), next(buffers), head(NULL), tail(NULL)
{
    buffers = this;
}

fieldbuf::~fieldbuf()
{
    assert(owner == tid::me());
    auto pprev(&buffers);
    while (*pprev != this) {
        assert(*pprev);
        pprev = &(*pprev)->next;
    }
    *pprev = this->next;
}

void
fieldbuf::reset()
{
    assert(owner == tid::me());
    assert(!!head == !!tail);
    head = NULL;
    tail = NULL;
}

void
fieldbuf::push(const char *what)
{
    assert(owner == tid::me());
    size_t sz(strlen(what) + 1);
    unsigned cursor(0);
    while (cursor != sz) {
        assert(cursor < sz);
        if (!tail || tail->used == sizeof(tail->content)) {
            auto f(alloc<fragment>());
            f->next = NULL;
            f->used = 0;
            if (tail)
                tail->next = f;
            else
                head = f;
            tail = f;
        }
        unsigned to_copy(min(sz - cursor, sizeof(tail->content) - tail->used));
        memcpy(tail->content + tail->used,
               what + cursor,
               to_copy);
        tail->used += to_copy;
        cursor += to_copy;
    }
    assert(tail->used > 0);
    tail->used--; /* take nul terminator back off */
}

char *
fieldbuf::c_str(maybe<unsigned> limit)
{
    if (!head)
        push("");
    assert(head);
    if (head == tail && (limit.isnothing() || tail->used <= limit.just()))
        return head->content;
    size_t sz(0);
    for (auto cursor(head);
         cursor && (limit.isnothing() || sz <= limit.just());
         cursor = cursor->next)
        sz += cursor->used;
    if (limit.isjust() && sz > limit.just())
        sz = limit.just();
    char *res((char *)_alloc(sz + 1));
    size_t used(0);
    for (auto cursor(head); cursor && used < sz; cursor = cursor->next) {
        unsigned to_copy(min<size_t>(cursor->used, sz - used));
        memcpy(res + used, cursor->content, to_copy);
        used += to_copy;
    }
    assert(sz == used);
    res[used] = '\0';
    return res;
}

field::field()
{}

void *
field::operator new(size_t sz)
{
    return _alloc(sz);
}

struct concfield : public field {
    const field &a;
    const field &b;
    concfield(const field &_a, const field &_b)
        : a(_a), b(_b)
        {}
    void fmt(fieldbuf &o) const
        {
            a.fmt(o);
            b.fmt(o);
        }
    static const field &n(const field &a, const field &b)
        { return *new concfield(a, b); }
};
const field &
operator +(const field &a, const field &b)
{
    return concfield::n(a, b);
}

struct truncfield : public field {
    const field &base;
    unsigned maxsize;
    truncfield(const field &_base, unsigned _maxsize)
        : base(_base), maxsize(_maxsize)
        { }
    void fmt(fieldbuf &out) const {
        fieldbuf inner;
        base.fmt(inner);
        char *c = inner.c_str(maybe<unsigned>(maxsize));
        out.push(c);
    }
    static const field &n(const field &what, unsigned maxsize)
    {
        return *new truncfield(what, maxsize);
    }
};
const field &
trunc(const field &what, unsigned maxsize)
{
    return truncfield::n(what, maxsize);
}

struct padfield : public field {
    const field &base;
    unsigned minsize;
    enum mode_t {
        pad_left,
        pad_right,
        pad_center,
    } mode;
    const field *padleft;
    const field *padright;
    padfield(const field &_base,
             unsigned _minsize,
             mode_t _mode,
             const field *_padleft,
             const field *_padright)
        : base(_base), minsize(_minsize), mode(_mode), padleft(_padleft),
          padright(_padright)
        {}
    static const field &n(const field &base,
                          unsigned minsize,
                          mode_t mode,
                          const field *padleft,
                          const field *padright)
        { return *new padfield(base, minsize, mode, padleft, padright); }
    void fmt(fieldbuf &out) const {
        fieldbuf basefmt;
        base.fmt(basefmt);
        char *basestr(basefmt.c_str());
        size_t baselen(strlen(basestr));
        if (baselen >= minsize) {
            out.push(basestr);
            return;
        }
        unsigned pad_chars(minsize - baselen);
        unsigned before_chars(
            mode == pad_left ? pad_chars :
            mode == pad_right ? 0 :
            mode == pad_center ? pad_chars / 2 :
            ({abort(); 0;}));
        unsigned emitted;
        if (before_chars) {
            assert(padleft);
            fieldbuf padfmt;
            padleft->fmt(padfmt);
            char *padstr(padfmt.c_str());
            size_t padlen(strlen(padstr));
            assert(padlen != 0);
            out.push(padstr + padlen - (before_chars % padlen));
            emitted = before_chars % padlen;
            while (emitted < before_chars) {
                assert(before_chars >= emitted + padlen);
                out.push(padstr);
                emitted += padlen;
            }
        } else {
            emitted = 0;
        }
        out.push(basestr);
        if (emitted < pad_chars) {
            assert(padright);
            fieldbuf padfmt;
            padright->fmt(padfmt);
            char *padstr(padfmt.c_str());
            size_t padlen(strlen(padstr));
            while (emitted + padlen <= pad_chars) {
                out.push(padstr);
                emitted += padlen;
            }
            if (emitted < pad_chars) {
                padstr[pad_chars - emitted] = '\0';
                out.push(padstr);
                emitted = pad_chars;
            }
        }
    }
};
const field &
padleft(const field &base, unsigned minsize, const field &pad)
{
    return padfield::n(base, minsize, padfield::pad_left, &pad, NULL);
}
const field &
padright(const field &base, unsigned minsize, const field &pad)
{
    return padfield::n(base, minsize, padfield::pad_right, NULL, &pad);
}
const field &
padcenter(const field &base, unsigned minsize,
          const field &padleft, const field &padright)
{
    return padfield::n(base, minsize, padfield::pad_center,
                       &padleft, &padright);
}

struct strfield : public field {
    const char *content;
    strfield(const char *_content)
        : content(_content)
        {}
    static const field &n(const char *content)
        { return *new strfield(content); }
    void fmt(fieldbuf &o) const
        { o.push(content); }
};
const field &
mk(const char *what)
{
    return strfield::n(what);
}
const field &
operator+(const char *what, const field &a)
{
    return strfield::n(what) + a;
}
const field &
operator+(const field &a, const char *what)
{
    return a + strfield::n(what);
}

intfield::intfield(long _val, int _base, bool _sep, bool _uppercase,
                   bool _alwayssign)
    : val_(_val), base_(_base), sep_(_sep), uppercase_(_uppercase),
      alwayssign_(_alwayssign)
{}
const intfield &
intfield::n(long val, int base, bool sep, bool uppercase, bool alwayssign)
{
    assert(base>1);
    assert(base<37);
    return *new intfield(val, base, sep, uppercase, alwayssign);
}
const intfield &
mk(long x)
{
    return intfield::n(x, 10, true, false, false);
}
const intfield &
intfield::base(int b) const
{
    return n(val_, b, sep_, uppercase_, alwayssign_);
}
const intfield &
intfield::nosep() const
{
    return n(val_, base_, false, uppercase_, alwayssign_);
}
const intfield &
intfield::uppercase() const
{
    return n(val_, base_, sep_, true, alwayssign_);
}
const intfield &
intfield::alwayssign() const
{
    return n(val_, base_, sep_, uppercase_, true);
}
void
intfield::fmt(fieldbuf &out) const
{
    long r;
    int nr_digits;
    r = val_;
    nr_digits = 0;
    if (r == 0) {
        nr_digits = 1;
    } else {
        while (r) {
            nr_digits++;
            r /= base_;
        }
    }
    /* Thousands seperators */
    if (sep_)
        nr_digits += (nr_digits - 1) / 3;
    /* base indicator */
    if (base_ < 10) {
        nr_digits += 3;
    } else if (base_ > 10) {
        nr_digits += 4;
    }
    /* Sign */
    if (alwayssign_ || val_ < 0)
        nr_digits++;
    /* nul terminator */
    nr_digits++;

    char buf[nr_digits];
    buf[--nr_digits] = 0;
    if (base_ != 10) {
        buf[--nr_digits] = '}';
        buf[--nr_digits] = "0123456789"[base_ % 10];
        if (base_ > 10)
            buf[--nr_digits] = "0123456789"[base_ / 10];
        buf[--nr_digits] = '{';
    }
    if (val_ == 0) {
        buf[--nr_digits] = '0';
    } else {
        r = val_;
        int cntr = 0;
        while (r) {
            int idx = r % base_;
            if (idx < 0)
                idx = -idx;
            buf[--nr_digits] =
                (uppercase_
                 ? "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                 : "0123456789abcdefghijklmnopqrstuvwxyz")
                [idx];
            r /= base_;
            cntr++;
            if (cntr == 3 && r != 0 && sep_) {
                buf[--nr_digits] = ',';
                cntr = 0;
            }
        }
    }
    if (val_ < 0 || alwayssign_)
        buf[--nr_digits] = (val_ < 0) ? '-' : '+';
    assert(nr_digits == 0);
    out.push(buf);
}

doublefield::doublefield(double _val)
    : field(), val_(_val)
{}
const doublefield &
doublefield::n(double val)
{
    return *new doublefield(val);
}
void
doublefield::fmt(fieldbuf &b) const
{
    char buf[64];
    unsigned r;
    r = snprintf(buf, sizeof(buf), "%f", val_);
    assert(r < sizeof(buf));
    b.push(buf);
}
const doublefield &
mk_double(double d)
{
    return doublefield::n(d);
}

timefield::timefield(const struct timeval &_v, bool _asdate)
    : v(_v), asdate_(_asdate)
{}
const timefield &
timefield::n(const struct timeval &v, bool asdate)
{
    return *new timefield(v, asdate);
}
const timefield &
timefield::asdate() const
{
    return n(v, true);
}
const timefield &
mk(const struct timeval &v)
{
    return timefield::n(v, false);
}
void
timefield::fmt(fieldbuf &buf) const
{
    if (asdate_) {
        struct tm v_tm;
        gmtime_r(&v.tv_sec, &v_tm);
        char time[128];
        int r;
        r = strftime(time,
                     sizeof(time),
                     "%F %T",
                     &v_tm);
        assert(r > 0);
        assert(r < (int)sizeof(time));
        buf.push(time);
    } else {
        mk(v.tv_sec).fmt(buf);
    }
    buf.push(".");
    padleft(mk(v.tv_usec).nosep(), 6, mk("0")).fmt(buf);
}

void
print(const field &f)
{
    fieldbuf buf;
    f.fmt(buf);
    printf("%s", buf.c_str());
}

void flush()
{
    while (arenas) {
        auto n(arenas->next);
        free(arenas);
        arenas = n;
    }
    for (auto n(buffers); n; n = n->next) {
        assert(n->owner == tid::me());
        n->reset();
        n->push("<flushed>");
    }
}

template const field &mk(const maybe<int> &);
template const field &mk(const maybe<const char *> &);

void test(::test &)
{
    flush();

    fieldbuf buf;
    mk("Hello world").fmt(buf);
    assert(!strcmp(buf.c_str(), "Hello world"));
    flush();
    assert(!strcmp(buf.c_str(), "<flushed>"));

    buf.reset();
    trunc(mk("Hello world"), 3).fmt(buf);
    assert(!strcmp(buf.c_str(), "Hel"));
    flush();

    buf.reset();
    padleft(trunc(mk("Hello world"), 3), 5).fmt(buf);
    assert(!strcmp(buf.c_str(), "  Hel"));
    flush();

    buf.reset();
    padright(trunc(mk("Hello world"), 3), 5).fmt(buf);
    assert(!strcmp(buf.c_str(), "Hel  "));
    flush();

    buf.reset();
    padcenter(trunc(mk("Hello world"), 3), 5).fmt(buf);
    assert(!strcmp(buf.c_str(), " Hel "));
    flush();

    buf.reset();
    padcenter(trunc(mk("Hello world"), 3), 10, mk("-->"), mk("<--"))
        .fmt(buf);
    assert(!strcmp(buf.c_str(), "-->Hel<--<"));
    flush();

    buf.reset();
    padcenter(trunc(mk("Hello world"), 3), 11, mk("-->"), mk("<--"))
        .fmt(buf);
    assert(!strcmp(buf.c_str(), ">-->Hel<--<"));
    flush();

    buf.reset();
    (mk("hello") + space + mk("world")).fmt(buf);
    assert(!strcmp(buf.c_str(), "hello world"));
    flush();

    buf.reset();
    mk(5).fmt(buf);
    assert(!strcmp(buf.c_str(), "5"));
    flush();

    buf.reset();
    mk(-5).fmt(buf);
    assert(!strcmp(buf.c_str(), "-5"));
    flush();

    buf.reset();
    mk(-5).base(2).fmt(buf);
    assert(!strcmp(buf.c_str(), "-101{2}"));
    flush();

    buf.reset();
    mk(-5).base(3).fmt(buf);
    assert(!strcmp(buf.c_str(), "-12{3}"));
    flush();

    buf.reset();
    mk(5).base(3).fmt(buf);
    assert(!strcmp(buf.c_str(), "12{3}"));
    flush();

    buf.reset();
    mk(10).base(16).fmt(buf);
    assert(!strcmp(buf.c_str(), "a{16}"));
    flush();

    buf.reset();
    mk(10).base(16).uppercase().fmt(buf);
    assert(!strcmp(buf.c_str(), "A{16}"));
    flush();

    buf.reset();
    mk(1000).fmt(buf);
    assert(!strcmp(buf.c_str(), "1,000"));
    flush();

    buf.reset();
    mk(1234567).fmt(buf);
    assert(!strcmp(buf.c_str(), "1,234,567"));
    flush();

    buf.reset();
    mk(72).base(2).fmt(buf);
    assert(!strcmp(buf.c_str(), "1,001,000{2}"));
    flush();

    buf.reset();
    mk(0x123456).base(16).fmt(buf);
    assert(!strcmp(buf.c_str(), "123,456{16}"));
    flush();

    buf.reset();
    mk(123456).nosep().fmt(buf);
    assert(!strcmp(buf.c_str(), "123456"));
    flush();
}

};
