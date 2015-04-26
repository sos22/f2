#include "parsers.H"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "either.H"
#include "error.H"
#include "fields.H"
#include "list.H"
#include "orerror.H"

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

class _longdoubleparser : public parser<long double> {
public:  orerror<result> parse(const char *) const;
};
orerror<_longdoubleparser::result>
_longdoubleparser::parse(const char *start) const {
    int n;
    long double r;
    int rr = sscanf(start, "%Lf%n", &r, &n);
    if (rr <= 0) return error::noparse;
    else return result(r, start + n); }

static _longdoubleparser __longdoubleparser;
const parser<long double> &parsers::longdoubleparser((__longdoubleparser));

namespace parsers {
template <> const parser<int> &
defaultparser<int>() {
    return intparser<int>(); } }
