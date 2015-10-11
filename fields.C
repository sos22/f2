#include "fields.H"

#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <string.h>

#include "list.H"
#include "tmpheap.H"
#include "util.H"

#include "fields.tmpl"
#include "list.tmpl"

namespace fields {

static struct : public field {
    void fmt(fieldbuf &p) const { p.push(" "); }
} _space;
const field &space(_space);
static struct : public field {
    void fmt(fieldbuf &p) const { p.push(","); }
} _comma;
const field &comma(_comma);
static struct : public field {
    void fmt(fieldbuf &p) const { p.push(":"); }
} _colon;
const field &colon(_colon);
static struct : public field {
    void fmt(fieldbuf &p) const { p.push("."); }
} _period;
const field &period(_period);

fieldbuf::fieldbuf()
    : head(NULL), tail(NULL)
{
}

void
fieldbuf::push(const char *what)
{
    size_t sz(strlen(what) + 1);
    unsigned cursor(0);
    while (cursor != sz) {
        assert(cursor < sz);
        if (!tail || tail->used == sizeof(tail->content)) {
            auto f(new fragment());
            f->next = NULL;
            f->used = 0;
            if (tail)
                tail->next = f;
            else
                head = f;
            tail = f;
        }
        unsigned short to_copy(
            (unsigned short)min(sz - cursor,
                                sizeof(tail->content) - tail->used,
                                0xffffu));
        memcpy(tail->content + tail->used,
               what + cursor,
               to_copy);
        tail->used = (unsigned short)(tail->used + to_copy);
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
    auto res((char *)tmpheap::_alloc(sz + 1));
    size_t used(0);
    for (auto cursor(head); cursor && used < sz; cursor = cursor->next) {
        auto to_copy((unsigned)min(
                         (size_t)cursor->used,
                         sz - used,
                         0xffffffff));
        memcpy(res + used, cursor->content, to_copy);
        used += to_copy;
    }
    assert(sz == used);
    res[used] = '\0';
    return res;
}

field::field()
{}

const field &
field::escape() const { return *this; }

const char *
field::c_str() const {
    fieldbuf buf;
    fmt(buf);
    return buf.c_str(); }

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
        auto pad_chars(minsize - baselen);
        auto before_chars(
            mode == pad_left ? pad_chars :
            mode == pad_right ? 0 :
            mode == pad_center ? pad_chars / 2 :
            ({abort(); 0;}));
        size_t emitted;
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

strfield::strfield(const char *what, bool _escape)
    : content(tmpheap::strdup(what)),
      escape_(_escape) {
    strcpy(content, what); }
const strfield &
strfield::n(const char *what) { return *new strfield(what, false); }
const strfield &
strfield::escape() const { return *new strfield(content, true); }
void
strfield::fmt(fieldbuf &buf) const {
    if (!escape_) {
        buf.push(content);
        return; }
    bool specials = false;
    int i;
    for (i = 0; !specials && content[i]; i++)
        specials |= !isalnum(content[i]) &&
            (content[i] != ':') &&
            (content[i] != '_') &&
            (content[i] != '-');
    specials |= i == 0;
    if (!specials) {
        buf.push(content);
        return; }
    /* XXX not what you'd call efficient.  Hopefully won't matter;
       this shouldn't be used on any hot paths, anyway. */
    buf.push("\"");
    for (i = 0; content[i]; i++) {
        if (content[i] == '\"') {
            buf.push("\\\"");
        } else if (content[i] == '\\') {
            buf.push("\\\\");
        } else if (!isprint(content[i])) {
            buf.push("\\x");
            char b[3];
            sprintf(b, "%02x", (unsigned char)content[i]);
            buf.push(b);
        } else {
            char b[2];
            b[0] = content[i];
            b[1] = 0;
            buf.push(b); } }
    buf.push("\""); }

const strfield &
mk(char *what) { return strfield::n(what); }
const strfield &
mk(const char *what) { return strfield::n(what); }
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

intfield::intfield(unsigned long _val, unsigned _base, const field &_sep,
                   unsigned _sepwidth, bool _uppercase, bool _alwayssign,
                   bool _hidebase, bool _signed)
    : val_(_val), base_(_base), sep_(_sep), sepwidth_(_sepwidth),
      uppercase_(_uppercase), alwayssign_(_alwayssign), hidebase_(_hidebase),
      signed_(_signed)
{}
const intfield &
intfield::n(unsigned long val, unsigned base, const field &sep,
            unsigned sepwidth, bool uppercase, bool alwayssign, bool hidebase,
            bool _signed)
{
    assert(base>1);
    assert(base<37);
    return *new intfield(val,
                         base,
                         sep,
                         sepwidth,
                         uppercase,
                         alwayssign,
                         hidebase,
                         _signed);
}
const intfield &
mk(long x)
{
    return intfield::n((unsigned long)x,
                       10,
                       comma,
                       3,
                       false,
                       false,
                       false,
                       true);
}
const intfield &
mk(unsigned long x)
{
    return intfield::n(x, 10, comma, 3, false, false, false, false);
}
const intfield &
intfield::base(unsigned b) const
{
    assert(b >= 2);
    assert(b <= 36);
    return n(val_,
             b,
             sep_,
             sepwidth_,
             uppercase_,
             alwayssign_,
             hidebase_,
             signed_);
}
const intfield &
intfield::nosep() const
{
    return sep(comma, 0);
}
const intfield &
intfield::sep(const field &newsep, unsigned sepwidth) const {
    return n(val_,
             base_,
             newsep,
             sepwidth,
             uppercase_,
             alwayssign_,
             hidebase_,
             signed_); }
const intfield &
intfield::uppercase() const
{
    return n(val_,
             base_,
             sep_,
             sepwidth_,
             true,
             alwayssign_,
             hidebase_,
             signed_);
}
const intfield &
intfield::alwayssign() const
{
    return n(val_,
             base_,
             sep_,
             sepwidth_,
             uppercase_,
             true,
             hidebase_,
             signed_);
}
const intfield &
intfield::hidebase() const {
    return n(val_,
             base_,
             sep_,
             sepwidth_,
             uppercase_,
             alwayssign_,
             true,
             signed_); }
void
intfield::fmt(fieldbuf &out) const
{
    size_t nr_digits;
    nr_digits = 0;
    if (val_ == 0) {
        nr_digits = 1;
    } else if (signed_) {
        long r = (long)val_;
        while (r) {
            nr_digits++;
            r /= base_;
        }
    } else {
        unsigned long r = val_;
        while (r) {
            nr_digits++;
            r /= base_;
        }
    }
    /* Thousands seperators */
    const char *sepstr = NULL;
    size_t seplen = 0;
    if (sepwidth_ != 0 && nr_digits > sepwidth_) {
        fieldbuf buf;
        sep_.fmt(buf);
        sepstr = buf.c_str();
        seplen = strlen(sepstr);
        nr_digits += seplen * ((nr_digits - 1) / sepwidth_);
    }
    /* base indicator */
    if (!hidebase_) {
        if (base_ < 10) nr_digits += 3;
        else if (base_ > 10) nr_digits += 4; }

    /* Sign */
    if (alwayssign_ || (signed_ && (long)val_ < 0))
        nr_digits++;
    /* nul terminator */
    nr_digits++;

    char buf[nr_digits];
    buf[--nr_digits] = 0;
    if (val_ == 0) {
        buf[--nr_digits] = '0';
    } else if (signed_) {
        long r = (long)val_;
        unsigned cntr = 0;
        while (r) {
            int idx = (int)(r % base_);
            if (idx < 0)
                idx = -idx;
            buf[--nr_digits] =
                (uppercase_
                 ? "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                 : "0123456789abcdefghijklmnopqrstuvwxyz")
                [idx];
            r /= base_;
            cntr++;
            if (cntr == sepwidth_ && r != 0 && sepstr) {
                memcpy(buf + nr_digits - seplen, sepstr, seplen);
                nr_digits -= seplen;
                cntr = 0;
            }
        }
    } else {
        unsigned long r = val_;
        unsigned cntr = 0;
        while (r) {
            unsigned idx = (unsigned)(r % base_);
            buf[--nr_digits] =
                (uppercase_
                 ? "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                 : "0123456789abcdefghijklmnopqrstuvwxyz")
                [idx];
            r /= base_;
            cntr++;
            if (cntr == sepwidth_ && r != 0 && sepstr) {
                memcpy(buf + nr_digits - seplen, sepstr, seplen);
                nr_digits -= seplen;
                cntr = 0;
            }
        }
    }
    if (alwayssign_ || (signed_ && (long)val_ < 0))
        buf[--nr_digits] = (signed_ && (long)val_ < 0) ? '-' : '+';
    if (!hidebase_ && base_ != 10) {
        buf[--nr_digits] = '}';
        buf[--nr_digits] = "0123456789"[base_ % 10];
        if (base_ > 10)
            buf[--nr_digits] = "0123456789"[base_ / 10];
        buf[--nr_digits] = '{'; }
    assert(nr_digits == 0);
    out.push(buf);
}

const field &
mk(bool b) {
    if (b) return mk("TRUE");
    else return mk("FALSE"); }

doublefield::doublefield(long double _val)
    : field(), val_(_val)
{}
const doublefield &
doublefield::n(long double val)
{
    return *new doublefield(val);
}
void
doublefield::fmt(fieldbuf &b) const
{
    char buf[64];
    int r;
    r = snprintf(buf, sizeof(buf), "%.*Lg", __DECIMAL_DIG__, val_);
    assert(r > 0);
    assert(r < (long)sizeof(buf));
    b.push(buf);
}
const doublefield &
mk_double(long double d)
{
    return doublefield::n(d);
}

void
print(const field &f)
{
    fieldbuf buf;
    f.fmt(buf);
    printf("%s", buf.c_str());
}

}
