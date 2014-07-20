#include "parsers.H"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "either.H"
#include "error.H"
#include "fields.H"
#include "orerror.H"
#include "test.H"

#include "either.tmpl"
#include "parsers.tmpl"

maybe<error>
parser<void>::match(const string &what) const {
    auto r1(parse(what.c_str()));
    if (r1.isfailure()) return r1.failure();
    else if (r1.success()[0] != '\0') return error::noparse;
    else return Nothing; }

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
};
orerror<parser<const char *>::result>
strparser_::parse(const char *what) const {
    if (what[0] == '\"') {
        int len = 0;
        int cursor = 1;
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
        int i = 0;
        cursor = 1;
        while (1) {
            assert(i <= len);
            if (what[cursor] == '\"') {
                res[i] = '\0';
                cursor++;
                break;
            } else if (what[cursor] == '\\') {
                if (what[cursor+1] == 'x') {
                    auto m([] (unsigned char c) {
                            if (c >= '0' && c <= '9') return c - '0';
                            else if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                            else if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                            else if (!COVERAGE) abort(); return -1; });
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
        int len;
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
const parser<const char *>&strparser(strparser_);

class intparser_ : public parser<long> {
public: orerror<result> parse(const char *) const;
};
orerror<parser<long>::result>
intparser_::parse(const char *what) const {
    bool negative = false;
    if (what[0] == '-') {
        negative = true;
        what++; }
    if (!isalnum(what[0])) return error::noparse;
    /* We only support default (i.e. ',') thousands separators. */
    /* Scan for a base marker */
    int i;
    for (i = 0; isalnum(what[i]) || (what[i] == ',' && isalnum(what[i+1])); i++)
        ;
    int nbase = 10;
    int basechars = 0;
    if (what[i] == '{') {
        /* Explicit base marker.  Parse it up. */
        nbase = 0;
        basechars = 2;
        i++;
        while (isdigit(what[i])) {
            /* Avoid overflow */
            if (nbase > 100) return error::noparse;
            nbase *= 10;
            nbase += what[i] - '0';
            basechars++;
            i++; }
        if (nbase < 2 || nbase > 36 || what[i] != '}') return error::noparse; }
    /* Now parse the number. */
    long acc = 0;
    i = 0;
    while (1) {
        int slot;
        bool must = false;
        if (what[i] == ',') {
            must = true;
            i++; }
        if (what[i] >= '0' && what[i] <= '9') slot = what[i] - '0';
        else if (what[i] >= 'a' && what[i] <= 'z') slot = what[i] - 'a' + 10;
        else if (what[i] >= 'A' && what[i] <= 'Z') slot = what[i] - 'A' + 10;
        else slot = -1;
        if (slot == -1 || slot >= nbase) {
            if (must) return error::noparse;
            if (basechars != 0 && what[i] != '{') return error::noparse;
            break; }
        assert( (acc * nbase) / nbase == acc);
        assert(acc + slot >= acc);
        acc = acc * nbase + slot;
        i++; }
    if (i == 0) return error::noparse;
    if (negative) acc = -acc;
    return result(acc, what + i + basechars); }
static const intparser_ intparser_;
const parser<long> &intparser(intparser_);

void
tests::parsers() {
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
            assert(strmatcher("ABC").match("ABC").isnothing());
            assert(strmatcher("ABC").match("A").just() == error::noparse);
            assert(strmatcher("ABC").match("ABCD").just() == error::noparse);
            assert(strmatcher("").match("A").just() == error::noparse);
            assert(strmatcher("").match("").isnothing()); });

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
                assert(r.success().left == ee + 6); } });

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

    testcaseV(
        "parsers",
        "intparser",
        [] () {
            assert(intparser.match("").failure() == error::noparse);
            assert(intparser.parse("ba").failure() == error::noparse);
            assert(intparser.match("7").success() == 7);
            assert(intparser.match("72").success() == 72);
            assert(intparser.match("-72").success() == -72);
            assert(intparser.match("123,456").success() == 123456);
            assert(intparser.match("ab").failure() == error::noparse);
            assert(intparser.match("ab{16}").success() == 0xab);});

    testcaseV(
        "parsers",
        "algebra+",
        [] () {
            assert( ("foo"+intparser).match("foo7").success() == 7);
            assert( (intparser + "bar").match("7bar").success() == 7);
            assert( !strcmp(
                        (strmatcher("foo")+strmatcher("bar"))
                        .parse("foobar")
                        .success(),
                        ""));
            auto r( (intparser + " " + intparser).match("83 -1"));
            assert(r.success().first() == 83);
            assert(r.success().second() == -1); });

    testcaseV(
        "parsers",
        "algebra|",
        [] () {
            assert( (strmatcher("foo")|intparser)
                    .match("foo")
                    .success()
                    .isnothing());
            assert( (strmatcher("foo")|intparser)
                    .match("83")
                    .success()
                    .just() == 83);
            assert( (strmatcher("foo")|intparser)
                    .match("foo83")
                    .failure()
                    == error::noparse);
            assert( (strmatcher("foo")|intparser)
                    .match("ZZZ")
                    .failure()
                    == error::noparse);
            assert( (intparser|strmatcher("bar"))
                    .match("7")
                    .success()
                    .just() == 7);
            assert( (intparser|strmatcher("bar"))
                    .match("bar")
                    .success()
                    .isnothing());
            assert( (intparser|strmatcher("bar"))
                    .match("91bar")
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
        "map",
        [] () {
            assert( intparser
                    .map<double>([] (int x) { return x * 5 + .3; })
                    .match("7")
                    .success() == 35.3);
            assert( errparser<int>(error::ratelimit)
                    .map<const char *>([] (int) { return "Huh?"; })
                    .match("7")
                    .failure() == error::ratelimit);});
}
