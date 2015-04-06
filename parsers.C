#include "parsers.H"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "either.H"
#include "error.H"
#include "fields.H"
#include "orerror.H"
#include "quickcheck.H"
#include "test.H"

#include "list.tmpl"
#include "parsers.tmpl"

orerror<void>
parser<void>::match(const string &what) const {
    auto r1(parse(what.c_str()));
    if (r1.isfailure()) return r1.failure();
    else if (r1.success()[0] != '\0') return error::noparse;
    else return Success; }

class matchthenmatch : public parser<void> {
private: const parser<void> &a;
private: const parser<void> &b;
public:  matchthenmatch(const parser<void> &_a,
                        const parser<void> &_b)
    : a(_a),
      b(_b) {}
private: orerror<const char *> parse(const char *) const; };
orerror<const char *>
matchthenmatch::parse(const char *x) const {
    auto r1(a.parse(x));
    if (r1.isfailure()) return r1.failure();
    return b.parse(r1.success()); }
const parser<void> &
parser<void>::operator+(const parser<void> &o) const {
    return *new matchthenmatch(*this, o); }

class matchormatch : public parser<void> {
private: const parser<void> &a;
private: const parser<void> &b;
public:  matchormatch(const parser<void> &_a,
                      const parser<void> &_b)
    : a(_a),
      b(_b) {}
private: orerror<const char *> parse(const char *) const; };
orerror<const char *>
matchormatch::parse(const char *x) const {
    auto r1(a.parse(x));
    if (r1.issuccess()) return r1;
    else return b.parse(x); }
const parser<void> &
parser<void>::operator|(const parser<void> &o) const {
    return *new matchormatch(*this, o); }

class optmatch : public parser<maybe<void> > {
private: const parser<void> &underlying;
public:  optmatch(const parser<void> &_underlying)
    : underlying(_underlying) {}
private: orerror<result> parse(const char*) const; };
orerror<optmatch::result>
optmatch::parse(const char *what) const {
    auto r(underlying.parse(what));
    if (r.isfailure()) return result(Nothing, what);
    else return result(maybe<void>::just, r.success()); }
const parser<maybe<void> > &
parser<void>::operator~() const {
    return *new optmatch(*this); }

class strmatchervoid : public parser<void> {
private: const char *what;
public:  strmatchervoid(const char *_what)
    : what(_what) {}
public:  orerror<const char *> parse(const char *) const;
};
orerror<const char *>
strmatchervoid::parse(const char *buf) const {
    size_t l(strlen(what));
    if (strncmp(buf, what, l) == 0) return buf + l;
    else return error::noparse; }
const parser<void> &
strmatcher(const char *what) {
    return *new strmatchervoid(what); }

class strparser_ : public parser<const char *> {
public: orerror<result> parse(const char *) const;
public: strparser_() : parser() {}
};
orerror<parser<const char *>::result>
strparser_::parse(const char *what) const {
    if (what[0] == '\"') {
        unsigned len = 0;
        unsigned cursor = 1;
        while (1) {
            if (what[cursor] == '\0') return error::noparse;
            if (what[cursor] == '\"') break;
            else if (what[cursor] == '\\') {
                if (what[cursor+1] == 'x' &&
                    isxdigit(what[cursor+2]) &&
                    isxdigit(what[cursor+3]) &&
                    (what[cursor+2] != '0' ||
                     what[cursor+3] != '0')) {
                    len++;
                    cursor += 4;
                } else if (what[cursor+1] == '\"') {
                    len++;
                    cursor += 2;
                } else if (what[cursor+1] == '\\') {
                    len++;
                    cursor += 2;
                } else {
                    return error::noparse; }
            } else if (isprint(what[cursor])) {
                len++;
                cursor++;
            } else {
                return error::noparse; } }
        char *res = (char *)tmpheap::_alloc(len + 1);
        unsigned i = 0;
        cursor = 1;
        while (1) {
            assert(i <= len);
            if (what[cursor] == '\"') {
                res[i] = '\0';
                cursor++;
                break;
            } else if (what[cursor] == '\\') {
                if (what[cursor+1] == 'x') {
                    auto m([] (char c) {
                            if (c >= '0' && c <= '9') return c - '0';
                            else if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                            else {
                                assert(c >= 'A' && c <= 'F');
                                return c - 'A' + 10; } } );
                    res[i] = (char)(m(what[cursor+2]) * 16 + m(what[cursor+3]));
                    i++;
                    cursor += 4;
                } else if (what[cursor+1] == '\"') {
                    res[i] = '\"';
                    i++;
                    cursor += 2;
                } else if (what[cursor+1] == '\\') {
                    res[i] = '\\';
                    i++;
                    cursor += 2;
                } else if (!COVERAGE) {
                    abort(); }
            } else {
                res[i++] = what[cursor++]; } }
        return result(res, what + cursor);
    } else {
        unsigned len;
        for (len = 0;
             isalnum(what[len])
                 || what[len] == ':'
                 || what[len] == '_'
                 || what[len] == '-';
             len++)
            ;
        if (len == 0) return error::noparse;
        auto res((char *)tmpheap::_alloc(len + 1));
        memcpy(res, what, len);
        res[len] = 0;
        return result(res, what + len); } }
static const strparser_ strparser_;
const parser<const char *>&parsers::strparser(strparser_);

template <typename typ, bool signd>
class intparser_ : public parser<typ> {
public: orerror<typename parser<typ>::result> parse(const char *) const;
};
template <typename typ, bool signd>
orerror<typename parser<typ>::result>
intparser_<typ, signd>::parse(const char *_what) const {
    /* We only support default (i.e. ',') thousands separators. */
    const char *what = _what;
    /* Parse base marker, if present */
    int nbase = 10;
    if (what[0] == '{') {
        if (!isdigit(what[1])) return error::noparse;
        if (what[2] == '}') {
            nbase = what[1] - '0';
            what += 3; }
        else if (isdigit(what[2]) && what[3] == '}') {
            nbase = (what[1] - '0') * 10 + (what[2] - '0');
            if (nbase > 36) return error::noparse;
            what += 4;
        } else {
            return error::noparse; } }
    auto validdigit([nbase] (char c) {
            return (c >= '0' && c < nbase + '0') ||
                (c >= 'a' && c < nbase + 'a' - 10) ||
                (c >= 'A' && c < nbase + 'A' - 10); });
    bool negative = false;
    if (signd) {
        if (what[0] == '-') {
            negative = true;
            what++;
        } else if (what[0] == '+') {
            what++; } }
    if (!validdigit(what[0])) return error::noparse;
    /* Special case for zeroes. */
    while (what[0] == '0') what++;
    if (!validdigit(what[0])) return typename parser<typ>::result(0, what);
    /* Now parse the number. */
    /* For signed types we always parse it as a negative number and
       then flip the sign at the end, if it's positive, to avoid
       overflow issues caused by the two's complement asymmetry. */
    typ acc = 0;
    while (1) {
        int slot;
        bool must = false;
        if (what[0] == ',') {
            must = true;
            what++; }
        if (what[0] >= '0' && what[0] <= '9') slot = what[0] - '0';
        else if (what[0] >= 'a' && what[0] <= 'z') slot = what[0] - 'a' + 10;
        else if (what[0] >= 'A' && what[0] <= 'Z') slot = what[0] - 'A' + 10;
        else slot = nbase;
        if (slot >= nbase) {
            if (must) return error::noparse;
            else break; }
        typ newacc;
        if (signd) {
            newacc = (typ)(acc * (typ)nbase - (typ)slot);
            if (newacc > acc) return error::overflowed;
        } else {
            newacc = (typ)(acc * (typ)nbase + (typ)slot);
            if (newacc < acc) return error::overflowed; }
        acc = newacc;
        what++; }
    if (signd && !negative) {
        if ((typ)acc == (typ)-acc) return error::overflowed;
        acc = (typ)-acc; }
    return typename parser<typ>::result(acc, what); }

namespace parsers {
template <>
const parser<long> &intparser() {
    return *new intparser_<long, true>(); }
template <>
const parser<unsigned long> &intparser() {
    return *new intparser_<unsigned long, false>(); }
template <>
const parser<int> &intparser() {
    return *new intparser_<int, true>(); }
template <>
const parser<unsigned int> &intparser() {
    return *new intparser_<unsigned int, false>(); }
template <>
const parser<short> &intparser() {
    return *new intparser_<short, true>(); }
template <>
const parser<unsigned short> &intparser() {
    return *new intparser_<unsigned short, false>(); }
template <>
const parser<char> &intparser() {
    return *new intparser_<char, true>(); }
}

class _doubleparser : public parser<double> {
public:  orerror<result> parse(const char *) const;
};
orerror<_doubleparser::result>
_doubleparser::parse(const char *start) const {
    int n;
    double r;
    int rr = sscanf(start, "%lf%n", &r, &n);
    if (rr <= 0) return error::noparse;
    else return result(r, start + n); }

static _doubleparser __doubleparser;
const parser<double> &parsers::doubleparser((__doubleparser));

namespace parsers {
template <> const parser<int> &
defaultparser<int>() {
    return intparser<int>(); } }

void
tests::parsers() {
    using namespace parsers;
    testcaseV(
        "parsers",
        "strmatcher",
        [] () {
            const char *e = "";
            const char *A = "A";
            const char *AB = "AB";
            assert(strmatcher("").parse(e).success() == e);
            assert(strmatcher("A").parse(e).failure() == error::noparse);
            assert(strmatcher("").parse(A).success() == A);
            assert(strmatcher("A").parse(A).success() == A + 1);
            assert(strmatcher("A").parse(AB).success() == AB+1);
            assert(strmatcher("A",7).match("A").success() == 7); });

    testcaseV(
        "parsers",
        "voidmatch",
        [] () {
            assert(strmatcher("ABC").match("ABC") == Success);
            assert(strmatcher("ABC").match("A") == error::noparse);
            assert(strmatcher("ABC").match("ABCD") == error::noparse);
            assert(strmatcher("").match("A") == error::noparse);
            assert(strmatcher("").match("") == Success); });

    testcaseV(
        "parsers",
        "strparser",
        [] () {
            const char *XXX = "XXX";
            const char *XXXYYY = "XXX YYY";
            const char *_123YYY = "123+YYY";
            assert(strparser.match("").failure() == error::noparse);
            {   auto r(strparser.parse(XXX));
                assert(!strcmp(r.success().res, "XXX"));
                assert(r.success().left == XXX+3); }
            {   auto r(strparser.parse(XXXYYY));
                assert(!strcmp(r.success().res, "XXX"));
                assert(r.success().left == XXXYYY+3); }
            {   auto r(strparser.parse(_123YYY));
                assert(!strcmp(r.success().res, "123"));
                assert(r.success().left == _123YYY+3); }
            assert(strparser.match("\"").failure() == error::noparse);
            {   const char *ee = "\"\"";
                auto r(strparser.parse(ee));
                assert(!strcmp(r.success().res, ""));
                assert(r.success().left == ee + 2); }
            {   const char *ee = "\"X\"";
                auto r(strparser.parse(ee));
                assert(!strcmp(r.success().res, "X"));
                assert(r.success().left == ee + 3); }
            {   const char *ee = "\"\\\\\"";
                auto r(strparser.parse(ee));
                assert(!strcmp(r.success().res, "\\"));
                assert(r.success().left == ee + 4); }
            assert(strparser.match("\"\\\"").failure() == error::noparse);
            {   const char *ee = "\"\\\\\"";
                auto r(strparser.parse(ee));
                assert(!strcmp(r.success().res, "\\"));
                assert(r.success().left == ee + 4); }
            {   const char *ee = "\"\\\"\"";
                auto r(strparser.parse(ee));
                assert(!strcmp(r.success().res, "\""));
                assert(r.success().left == ee + 4); }
            {   const char *ee = "\"\\x07\"";
                auto r(strparser.parse(ee));
                assert(!strcmp(r.success().res, "\x07"));
                assert(r.success().left == ee + 6); }
            {   const char *ee = "\"\\xaa\"";
                auto r(strparser.parse(ee));
                assert(!strcmp(r.success().res, "\xaa"));
                assert(r.success().left == ee + 6); }
            {   const char *ee = "\"\\xAA\"";
                auto r(strparser.parse(ee));
                assert(!strcmp(r.success().res, "\xAA"));
                assert(r.success().left == ee + 6); }
            assert(strparser.match("\"\\xZ\"") == error::noparse);
            assert(strparser.match("\"\\F\"") == error::noparse);
            assert(strparser.match("\"\x01\"") == error::noparse); });

    testcaseV(
        "parsers",
        "fuzzstrparser",
        [] () {
            for (unsigned x = 0; x < 1000; x++) {
                const char *s = quickcheck();
                ::fields::fieldbuf b;
                ::fields::mk(s).escape().fmt(b);
                auto res(strparser.match(b.c_str()));
                assert(res.issuccess());
                assert(!strcmp(res.success(), s)); } });

    testcaseV(
        "parsers",
        "errorparser",
        [] () {
            auto r(errparser<int>(error::underflowed).parse((const char *)27));
            assert(r.failure() == error::underflowed);});

    auto ip(intparser<long>);

    testcaseV(
        "parsers",
        "intparser",
        [ip] () {
            assert(ip().match("").failure() == error::noparse);
            assert(ip().parse("ba").failure() == error::noparse);
            assert(ip().match("7").success() == 7);
            assert(ip().match("72").success() == 72);
            assert(ip().match("-72").success() == -72);
            assert(ip().match("123,456").success() == 123456);
            assert(ip().match("ab").failure() == error::noparse);
            assert(ip().match("{16}ab").success() == 0xab);
            assert(ip().match("{16}AB").success() == 0xab);
            assert(ip().match("{16}Ab").success() == 0xab);
            assert(ip().match("+7") == 7);
            assert(ip().match("000000") == 0);
            assert(ip().match("{5}0") == 0);
            assert(ip().match("{52}0") == error::noparse);
            assert(ip().match("{Z}0") == error::noparse);
            assert(ip().match("{93}1") == error::noparse);
            assert(ip().match("{1") == error::noparse); });

    testcaseV(
        "parsers",
        "algebra+",
        [ip] () {
            assert( ("foo"+ip()).match("foo7").success() == 7);
            assert( (ip() + "bar").match("7bar").success() == 7);
            assert( !strcmp(
                        (strmatcher("foo")+strmatcher("bar"))
                        .parse("foobar")
                        .success(),
                        ""));
            auto r( (ip() + " " + ip()).match("83 -1"));
            assert(r.success().first() == 83);
            assert(r.success().second() == -1); });

    testcaseV(
        "parsers",
        "algebra|",
        [ip] () {
            assert( (strmatcher("foo")|ip())
                    .match("foo")
                    .success()
                    .isnothing());
            assert( (strmatcher("foo")|ip())
                    .match("83")
                    .success()
                    .just() == 83);
            assert( (strmatcher("foo")|ip())
                    .match("foo83")
                    .failure()
                    == error::noparse);
            assert( (strmatcher("foo")|ip())
                    .match("ZZZ")
                    .failure()
                    == error::noparse);
            assert( (ip()|ip())
                    .match("ZZZ")
                    .failure()
                    == error::noparse);
            assert( (ip()|strmatcher("bar"))
                    .match("7")
                    .success()
                    .just() == 7);
            assert( (ip()|strmatcher("bar"))
                    .match("bar")
                    .success()
                    .isnothing());
            assert( (ip()|strmatcher("bar"))
                    .match("91bar")
                    .failure()
                    == error::noparse);
            assert( (ip()|strmatcher("bar"))
                    .match("dgha")
                    .failure()
                    == error::noparse);
            assert((strmatcher("foo")|strmatcher("bar"))
                   .parse("foo")
                   .issuccess());
            assert((strmatcher("foo")|strmatcher("bar"))
                   .parse("bar")
                   .issuccess());
            assert((strmatcher("foo")|strmatcher("bar"))
                   .parse("bazz")
                   .failure() == error::noparse);
            assert((strmatcher("foo")|strmatcher("bar")|strmatcher("bazz"))
                   .parse("foo")
                   .issuccess());
            assert((strmatcher("foo")|strmatcher("bar")|strmatcher("bazz"))
                   .parse("bar")
                   .issuccess());
            assert((strmatcher("foo")|strmatcher("bar")|strmatcher("bazz"))
                   .parse("bazz")
                   .issuccess());
            assert((strmatcher("foo")|strmatcher("bar")|strmatcher("bazz"))
                   .parse("boom")
                   .failure() == error::noparse);
            assert((strmatcher("foo", 83)|strmatcher("bar",
                                                     (const char *)"hello"))
                   .match("foo")
                   .success()
                   .left() == 83);
            assert(!strcmp(
                       (strmatcher("foo", 83)|strmatcher("bar",
                                                         (const char *)"hello"))
                       .match("bar")
                       .success()
                       .right(),
                       "hello")); });

    testcaseV(
        "parsers",
        "algebra||",
        [] () {
            auto &p(strmatcher("X").val(6) ||
                    strmatcher("Y").val(7));
            assert(p.match("X") == 6);
            assert(p.match("Y") == 7);
            assert(p.match("Z") == error::noparse); });

    testcaseV(
        "parsers",
        "algebra~",
        [] {
            auto &p(~strmatcher("X", 5) +
                    intparser<unsigned>() +
                    ~strmatcher("Y"));
            {   auto r(p.match("73"));
                assert(r.success().first().first() == Nothing);
                assert(r.success().first().second() == 73);
                assert(r.success().second() == Nothing); }
            {   auto r(p.match("73Y"));
                assert(r.success().first().first() == Nothing);
                assert(r.success().first().second() == 73);
                assert(r.success().second() != Nothing); }
            {   auto r(p.match("X73"));
                assert(r.success().first().first().just() == 5);
                assert(r.success().first().second() == 73);
                assert(r.success().second() == Nothing); }
            {   auto r(p.match("X73Y"));
                assert(r.success().first().first().just() == 5);
                assert(r.success().first().second() == 73);
                assert(r.success().second() != Nothing); }
            assert(p.match("X") == error::noparse);
            assert(p.match("Y") == error::noparse); });

    testcaseV(
        "parsers",
        "intedges",
        [] () {
            /* Surprise! Clang does the wrong thing with integer
               constants which are the most negative value for their
               type. */
            assert(intparser<long>()
                   .match("{16}-8000,0000,0000,0000")
                   == -0x7fffffffffffffffl - 1);
            assert(intparser<long>()
                   .match("{16}-8000,0000,0000,0001")
                   == error::overflowed);
            assert(intparser<long>()
                   .match("{16}8000,0000,0000,0000")
                   == error::overflowed);
            assert(intparser<long>()
                   .match("{16}7fff,ffff,ffff,ffff")
                   == 0x7fffffffffffffffl);
            assert(intparser<int>()
                   .match("{16}-8000,0000")
                   == -0x7fffffff - 1);
            assert(intparser<int>()
                   .match("{16}-8000,0001")
                   == error::overflowed);
            assert(intparser<int>()
                   .match("{16}8000,0000")
                   == error::overflowed);
            assert(intparser<int>()
                   .match("{16}7fff,ffff")
                   == 0x7fffffff);
            assert(intparser<short>().match("{16}-8000") == -0x8000);
            assert(intparser<short>().match("{16}-8001") == error::overflowed);
            assert(intparser<short>().match("{16}8000") == error::overflowed);
            assert(intparser<short>().match("{16}7fff") == 0x7fffl);
            assert(intparser<char>().match("{16}-80") == -0x80);
            assert(intparser<char>().match("{16}-81") == error::overflowed);
            assert(intparser<char>().match("{16}80") == error::overflowed);
            assert(intparser<char>().match("{16}7f") == 0x7fl);

            assert(intparser<long>().match("0") == 0);
            assert(intparser<long>().match("000000") == 0);
            assert(intparser<unsigned long>().match("0") == 0);
            assert(intparser<unsigned long>().match("00") == 0);
            assert(intparser<unsigned long>().match("001") == 1);
            assert((intparser<long>() + "ns").match("0ns") == 0);

            assert(intparser<unsigned long>().match("{16}ffff,ffff,ffff,ffff")
                   == 0xfffffffffffffffful);
            assert(intparser<unsigned long>().match("{16}1,0000,0000,0000,0000")
                   == error::overflowed);
            assert(intparser<unsigned>().match("{16}ffff,ffff")
                   == 0xfffffffful);
            assert(intparser<unsigned>().match("{16}1,0000,0000")
                   == error::overflowed);
            assert(intparser<unsigned short>().match("{16}ffff")
                   == 0xfffful);
            assert(intparser<unsigned short>().match("{16}1,0000")
                   == error::overflowed);

            /* Base 36 is acceptable, base 37 isn't. */
            assert(intparser<unsigned>().match("{36}Za")
                   == 1270);
            assert(intparser<unsigned>().match("{37}Za")
                   == error::noparse); });

    testcaseV("parsers", "double", [] {
            assert(doubleparser.match("7.25") == 7.25);
            assert(doubleparser.match("-1") == -1);
            assert(doubleparser.match("Z") == error::noparse); });

    testcaseV(
        "parsers",
        "nul",
        [] () {
            assert(nulparser(73).match("") == 73);
            assert(nulparser(73).match("x") == error::noparse); });

    testcaseV(
        "parsers",
        "map",
        [ip] () {
            assert( ip()
                    .map<double>([] (int x) { return x * 5 + .3; })
                    .match("7")
                    .success() == 35.3);
            assert( errparser<int>(error::ratelimit)
                    .map<const char *>([] (int) { return "Huh?"; })
                    .match("7")
                    .failure() == error::ratelimit);});
    testcaseV(
        "parsers",
        "maperr",
        [] {
            assert(intparser<unsigned>()
                   ._maperr<double>(
                       [] (const orerror<unsigned> &x) {
                           return x.failure(); })
                   .match("Z") == error::noparse);
            assert(intparser<unsigned>()
                   ._maperr<double>(
                       [] (const orerror<unsigned> &x) {
                           assert(x.success() == 73);
                           return error::noparse; })
                   .match("73") == error::noparse);
            assert(intparser<unsigned>()
                   ._maperr<double>(
                       [] (const orerror<unsigned> &x) {
                           assert(x.success() == 73);
                           return 92.5; })
                   .match("73") == 92.5);
            assert(intparser<unsigned>()
                   ._maperr<double>(
                       [] (const orerror<unsigned> &x) {
                           assert(x == error::noparse);
                           return 92.25; })
                   .match("") == 92.25);
            assert(intparser<unsigned>()
                   .maperr<double>(
                       [] (const unsigned &x) {
                           return x + 5; })
                   .match("12") == 17);
            assert(intparser<unsigned>()
                   .maperr<double>(
                       [] (const unsigned &) {
                           return error::overflowed; })
                   .match("12") == error::overflowed); });

    testcaseV("parsers", "mapvoid", [] {
            int cntr;
            cntr = 0;
            assert(strmatcher("Foo")
                   .map<int>([&cntr] { return cntr++; })
                   .match("Foo") == 0);
            assert(strmatcher("Foo")
                   .map<int>([&cntr] { return cntr++; })
                   .match("Foo") == 1);
            assert(strmatcher("Foo")
                   .map<int>([&cntr] { return cntr++; })
                   .match("Foo") == 2);
            assert(strmatcher("Foo")
                   .map<int>([&cntr] { return cntr++; })
                   .match("Bar") == error::noparse); });

    testcaseV("parsers", "roundtrip", [] {
            parsers::roundtrip(intparser<unsigned>()); });
    testcaseV("parsers", "sepby", [] {
            auto &p(parsers::sepby(parsers::intparser<int>(),
                                   strmatcher(" ")));
            assert(p.match("") == list<int>::mk());
            assert(p.match("1") == list<int>::mk(1));
            assert(p.match("1 2 3") == list<int>::mk(1, 2, 3));
            assert(p.match("1 2 3 ") == error::noparse);
            assert(p.match(" 1 2 3") == error::noparse);
            assert((strmatcher("YYY") + p + strmatcher("XXX"))
                   .match("YYY1 2 3XXX") ==
                   list<int>::mk(1, 2, 3)); }); }
