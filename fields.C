#include "fields.H"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "list.H"
#include "test.H"
#include "spark.H"
#include "tmpheap.H"
#include "util.H"

#include "list.tmpl"

#include "fieldfinal.H"

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
    void fmt(fieldbuf &p) const { p.push("."); }
} _period;
const field &period(_period);

fieldbuf::fieldbuf()
    : head(NULL), tail(NULL)
{
}

void
fieldbuf::reset()
{
    assert(!!head == !!tail);
    head = NULL;
    tail = NULL;
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

strfield::strfield(const char *what, bool _escape)
    : content(tmpheap::strdup(what)),
      escape_(_escape) {
    strcpy(content, what); }
const strfield &
strfield::n(const char *what) {
    return *new strfield(what, false); }
const strfield &
strfield::escape() const {
    return *new strfield(content, true); }
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

intfield::intfield(long _val, int _base, const field &_sep, unsigned _sepwidth,
                   bool _uppercase, bool _alwayssign, bool _hidebase)
    : val_(_val), base_(_base), sep_(_sep), sepwidth_(_sepwidth),
      uppercase_(_uppercase), alwayssign_(_alwayssign), hidebase_(_hidebase)
{}
const intfield &
intfield::n(long val, int base, const field &sep, unsigned sepwidth,
            bool uppercase, bool alwayssign, bool hidebase)
{
    assert(base>1);
    assert(base<37);
    return *new intfield(val,
                         base,
                         sep,
                         sepwidth,
                         uppercase,
                         alwayssign,
                         hidebase);
}
const intfield &
mk(long x)
{
    return intfield::n(x, 10, comma, 3, false, false, false);
}
const intfield &
intfield::base(int b) const
{
    assert(b >= 2);
    assert(b <= 36);
    return n(val_, b, sep_, sepwidth_, uppercase_, alwayssign_, hidebase_);
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
             hidebase_); }
const intfield &
intfield::uppercase() const
{
    return n(val_, base_, sep_, sepwidth_, true, alwayssign_, hidebase_);
}
const intfield &
intfield::alwayssign() const
{
    return n(val_, base_, sep_, sepwidth_, uppercase_, true, hidebase_);
}
const intfield &
intfield::hidebase() const {
    return n(val_, base_, sep_, sepwidth_, uppercase_, alwayssign_, true); }
void
intfield::fmt(fieldbuf &out) const
{
    long r;
    size_t nr_digits;
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
        if (base_ < 10)  nr_digits += 3;
        else if (base_ > 10) nr_digits += 4; }

    /* Sign */
    if (alwayssign_ || val_ < 0)
        nr_digits++;
    /* nul terminator */
    nr_digits++;

    char buf[nr_digits];
    buf[--nr_digits] = 0;
    if (!hidebase_ && base_ != 10) {
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
        auto r = strftime(time,
                     sizeof(time),
                     "%F %T",
                     &v_tm);
        assert(r > 0);
        assert(r < sizeof(time));
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

}

void
tests::fields()
{
    using namespace fields;
    testcaseV("fields", "helloworld",
        [] () {
            fieldbuf buf;
            mk("Hello world").fmt(buf);
            assert(!strcmp(buf.c_str(), "Hello world"));});

    testcaseV("fields", "trunc",
        [] () {
            fieldbuf buf;
            trunc(mk("Hello world"), 3).fmt(buf);
            assert(!strcmp(buf.c_str(), "Hel")); });

    testcaseV("fields", "padleft",
             [] () {
                 fieldbuf buf;
                 padleft(trunc(mk("Hello world"), 3), 5).fmt(buf);
                 assert(!strcmp(buf.c_str(), "  Hel")); });

    testcaseV("fields", "padright",
             [] () {
                 fieldbuf buf;
                 padright(trunc(mk("Hello world"), 3), 5).fmt(buf);
                 assert(!strcmp(buf.c_str(), "Hel  ")); });

    testcaseV("fields", "padcenter",
             [] () {
                 fieldbuf buf;
                 padcenter(trunc(mk("Hello world"), 3), 5).fmt(buf);
                 assert(!strcmp(buf.c_str(), " Hel ")); });

    testcaseV("fields", "arrowpad1", [] () {
            fieldbuf buf;
            padcenter(trunc(mk("Hello world"), 3), 10, mk("-->"), mk("<--"))
                .fmt(buf);
            assert(!strcmp(buf.c_str(), "-->Hel<--<"));  });

    testcaseV("fields", "arrowpad2", [] () {
            fieldbuf buf;
            padcenter(trunc(mk("Hello world"), 3), 11, mk("-->"), mk("<--"))
                .fmt(buf);
            assert(!strcmp(buf.c_str(), ">-->Hel<--<"));  });

    testcaseV("fields", "strconcat", [] () {
            fieldbuf buf;
            (mk("hello") + space + mk("world")).fmt(buf);
            assert(!strcmp(buf.c_str(), "hello world"));  });

    testcaseV("fields", "integers", [] () {
            fieldbuf buf;
            mk(5).fmt(buf);
            assert(!strcmp(buf.c_str(), "5"));  });

    testcaseV("fields", "negint", [] () {
            fieldbuf buf;
            mk(-5).fmt(buf);
            assert(!strcmp(buf.c_str(), "-5"));  });

    testcaseV("fields", "negbinint", [] () {
            fieldbuf buf;
            mk(-5).base(2).fmt(buf);
            assert(!strcmp(buf.c_str(), "-101{2}"));  });

    testcaseV("fields", "negtrinint", [] () {
            fieldbuf buf;
            mk(-5).base(3).fmt(buf);
            assert(!strcmp(buf.c_str(), "-12{3}"));  });

    testcaseV("fields", "trinint", [] () {
            fieldbuf buf;
            mk(5).base(3).fmt(buf);
            assert(!strcmp(buf.c_str(), "12{3}"));  });

    testcaseV("fields", "hexint", [] () {
            fieldbuf buf;
            mk(10).base(16).fmt(buf);
            assert(!strcmp(buf.c_str(), "a{16}"));  });

    testcaseV("fields", "HEXint", [] () {
            fieldbuf buf;
            mk(10).base(16).uppercase().fmt(buf);
            assert(!strcmp(buf.c_str(), "A{16}"));  });

    testcaseV("fields", "thousandsep", [] () {
            fieldbuf buf;
            mk(1000).fmt(buf);
            assert(!strcmp(buf.c_str(), "1,000"));  });

    testcaseV("fields", "thousandsep2", [] () {
            fieldbuf buf;
            mk(1234567).fmt(buf);
            assert(!strcmp(buf.c_str(), "1,234,567"));  });

    testcaseV("fields", "binthousandsep", [] () {
            fieldbuf buf;
            mk(72).base(2).fmt(buf);
            assert(!strcmp(buf.c_str(), "1,001,000{2}"));  });

    testcaseV("fields", "hexthousandsep", [] () {
            fieldbuf buf;
            mk(0x123456).base(16).fmt(buf);
            assert(!strcmp(buf.c_str(), "123,456{16}"));  });

    testcaseV("fields", "nosep", [] () {
            fieldbuf buf;
            mk(123456).nosep().fmt(buf);
            assert(!strcmp(buf.c_str(), "123456"));  });

    testcaseV("fields", "complexsep", [] () {
            fieldbuf buf;
            mk(123456).sep(fields::mk("ABC"), 1).fmt(buf);
            assert(!strcmp(buf.c_str(), "1ABC2ABC3ABC4ABC5ABC6"));  });

    testcaseV("fields", "hidebase", [] () {
            assert(strcmp(fields::mk(0xaabb).base(16).hidebase().c_str(),
                          "a,abb") == 0); });

    testcaseV("fields", "period", [] () {
            fieldbuf buf;
            period.fmt(buf);
            assert(!strcmp(buf.c_str(), ".")); });

    testcaseV("fields", "threads", [] () {
            fieldbuf buf1;
            mk("Hello").fmt(buf1);
            assert(!strcmp(buf1.c_str(), "Hello"));
            /* Flushing from another thread shouldn't affect this
             * one. */
            spark<bool> w([] () {
                    fieldbuf buf2;
                    mk("goodbye").fmt(buf2);
                    assert(!strcmp(buf2.c_str(), "goodbye"));
                    tmpheap::release();
                    fieldbuf buf3;
                    mk("doomed").fmt(buf3);
                    return true; });
            w.get();
            assert(!strcmp(buf1.c_str(), "Hello")); });

    testcaseV("fields", "bigstr", [] () {
            char *f = (char *)malloc(1000001);
            memset(f, 'c', 1000000);
            f[1000000] = 0;
            fieldbuf buf1;
            mk(f).fmt(buf1);
            assert(!strcmp(buf1.c_str(), f));
            free(f);});

    testcaseV("fields", "lotsofbufs", [] () {
            fieldbuf buf;
            const field *acc = &mk("");
            for (int i = 0; i < 100; i++) {
                acc = &(*acc + padleft(mk("hello"), 1000)); }
            acc->fmt(buf);
            char reference[1001];
            reference[1000] = 0;
            memset(reference, ' ', 1000);
            memcpy(reference+995, "hello", 5);
            for (int i = 0; i < 100; i++) {
                assert(!memcmp(buf.c_str() + i * 1000,
                               reference,
                               1000)); } });

    testcaseV("fields", "empty", [] () {
            fieldbuf buf;
            assert(!strcmp(buf.c_str(), "")); });

    testcaseV("fields", "padnoop", [] () {
            fieldbuf buf;
            padleft(mk("foo"), 1).fmt(buf);
            assert(!strcmp(buf.c_str(), "foo")); });

    testcaseV("fields", "conc1", [] () {
            fieldbuf buf;
            (mk("foo") + "bar").fmt(buf);
            assert(!strcmp(buf.c_str(), "foobar")); });

    testcaseV("fields", "conc2", [] () {
            fieldbuf buf;
            ("foo" + mk("bar")).fmt(buf);
            assert(!strcmp(buf.c_str(), "foobar")); });

    testcaseV("fields", "alwayssign", [] () {
            fieldbuf buf;
            mk(5).alwayssign().fmt(buf);
            assert(!strcmp(buf.c_str(), "+5")); });

    testcaseV("fields", "zero", [] () {
            fieldbuf buf;
            mk((long)0).fmt(buf);
            assert(!strcmp(buf.c_str(), "0")); });

    testcaseV("fields", "double1", [] () {
            fieldbuf buf;
            mk_double(5.0).fmt(buf);
            assert(!strcmp(buf.c_str(), "5.000000")); });

    testcaseV("fields", "multibufs", [] () {
            auto buf1(new fieldbuf());
            auto buf2(new fieldbuf());
            auto buf3(new fieldbuf());
            delete buf2;
            delete buf1;
            delete buf3;});

    testcaseV("fields", "ts1", [] () {
            fieldbuf buf;
            struct timeval tv = {72, 99};
            mk(tv).fmt(buf);
            assert(!strcmp(buf.c_str(), "72.000099"));});

    testcaseV("fields", "ts2", [] () {
            fieldbuf buf;
            struct timeval tv = {72, 99};
            mk(tv).asdate().fmt(buf);
            assert(!strcmp(buf.c_str(), "1970-01-01 00:01:12.000099"));});

    testcaseV("fields", "ts3", [] () {
            fieldbuf buf;
            struct timeval tv = {1404546593, 123456};
            mk(tv).asdate().fmt(buf);
            assert(!strcmp(buf.c_str(), "2014-07-05 07:49:53.123456"));});

    testcaseV("fields", "maybe", [] () {
            fieldbuf buf;
            ::maybe<int> x(Nothing);
            mk(x).fmt(buf);
            assert(!strcmp(buf.c_str(), "Nothing"));
            buf.reset();
            x = 91;
            mk(x).fmt(buf);
            assert(!strcmp(buf.c_str(), "<91>")); });

    testcaseV("fields", "orerror", [] () {
            fieldbuf buf;
            orerror<int> x(error::disconnected);
            mk(x).fmt(buf);
            assert(!strcmp(buf.c_str(), "<failed:disconnected>"));
            buf.reset();
            x = 18;
            mk(x).fmt(buf);
            assert(!strcmp(buf.c_str(), "<18>"));});

    testcaseV("fields", "list", [] () {
            fieldbuf buf;
            list<int> l;
            mk(l).fmt(buf);
            assert(!strcmp(buf.c_str(), "{}"));
            l.pushtail(1);
            buf.reset();
            mk(l).fmt(buf);
            assert(!strcmp(buf.c_str(), "{1}"));
            l.pushtail(12);
            buf.reset();
            mk(l).fmt(buf);
            assert(!strcmp(buf.c_str(), "{1 12}"));
            l.flush(); });

    testcaseV("fields", "escape", [] () {
            fieldbuf buf;
            mk("foo").escape().fmt(buf);
            assert(!strcmp(buf.c_str(), "foo"));
            buf.reset();

            mk("foo bar").escape().fmt(buf);
            assert(!strcmp(buf.c_str(), "\"foo bar\""));
            buf.reset();

            mk("\"foo bar\"").escape().fmt(buf);
            assert(!strcmp(buf.c_str(), "\"\\\"foo bar\\\"\""));
            buf.reset();

            mk("foo\"bar").escape().fmt(buf);
            assert(!strcmp(buf.c_str(), "\"foo\\\"bar\""));
            buf.reset();

            mk("foo+bar").escape().fmt(buf);
            assert(!strcmp(buf.c_str(), "\"foo+bar\""));
            buf.reset();

            mk("").escape().fmt(buf);
            assert(!strcmp(buf.c_str(), "\"\""));
            buf.reset();

            mk("\\").escape().fmt(buf);
            assert(!strcmp(buf.c_str(), "\"\\\\\""));
            buf.reset();

            mk("\t").escape().fmt(buf);
            assert(!strcmp(buf.c_str(), "\"\\x09\""));
            buf.reset();

            mk("ZZZ\xffXXX").escape().fmt(buf);
            assert(!strcmp(buf.c_str(), "\"ZZZ\\xffXXX\""));
            buf.reset(); });

    testcaseV("fields", "c_str", [] () {
            assert(!strcmp(fields::mk(5).c_str(), "5")); });

    /* Not really a useful test case, but it makes the coverage 100%,
     * and the print() function's simple enough that just confirming
     * it doesn't crash is good enough. */
    testcaseV("fields", "print", [] () {
            print(mk("hello\n")); });
}
