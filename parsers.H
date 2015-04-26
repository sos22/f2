#ifndef PARSERS_H__
#define PARSERS_H__

#include <functional>

#include "error.H"
#include "maybe.H"
#include "pair.H"
#include "tmpheap.H"

template <typename, typename> class either;
class filename;
template <typename> class list;
template <typename> class orerror;
template <typename, typename> class pair;
class string;

/* Class for things which can take strings and turn them into
 * rvals. */
template <typename rval>
class parser : public tmpheap::base {
    /* Partial parse result: represents the value of the current
       parser and all of the bits it didn't parse. */
public: class result {
    public: rval res;
    public: const char *left;
    public: result(const rval &a, const char *b); };
    /* Parse a string, returning either the result of the parse or an
     * error. */
public: virtual orerror<result> parse(const char *) const = 0;
    /* Lift a function into the parser monad. */
public: template <typename o> const parser<o> &map(
    const std::function<o (const rval &)> &what) const;
    /* Like map, except that it can also see error values. */
public: template <typename o> const parser<o> &_maperr(
    const std::function<orerror<o> (const orerror<rval> &)> &what) const;
    /* Like map, except that it can return an error. */
public: template <typename o> const parser<o> &maperr(
    const std::function<orerror<o> (const rval &)> &what) const;
    /* Convenience function which runs the parser and returns either
       the result, if it matched the entire string, or an error, if it
       didn't. */
public: orerror<rval> match(const string &) const;
    /* Concatenate two parsers. */
public: template <typename other> const parser< pair<rval, other> > &
    operator + (const parser<other> &o) const;
public: const parser<rval> &operator+(const parser<void> &o) const;

    /* Parser alternation.  Note that this has no look-ahead, so once
       something's matched we won't backtrack for later failures. */
public: template <typename other> const parser< either<rval, other> > &
    operator | (const parser<other> &o) const;
public: const parser<maybe<rval> > &operator|(const parser<void> &o) const;

    /* Optional parsers. */
public: const parser<maybe<rval> > &operator~() const;
};

/* Special case for parsers which don't return anything. */
template <>
class parser<void> : public tmpheap::base {
public: typedef const char *result;
public: virtual orerror<const char *> parse(const char *) const = 0;
public: template <typename o> const parser<o> &map(
    const std::function<o ()> &what) const;
public: orerror<void> match(const string &) const;
public: template <typename t> const parser<t> &val(const t &) const;
/* Concatenation and alternation have simpler types here. */
public: template <typename t> const parser<t> &operator+(
    const parser<t> &) const;
public: template <typename t> const parser<maybe<t> > &operator|(
    const parser<t> &) const;
public: const parser<void> &operator+(const parser<void> &) const;
public: const parser<void> &operator|(const parser<void> &) const;
public: const parser<maybe<void> > &operator~() const;
};

/* Special case for parsers which always succeed without consuming any
 * input. */
template <typename t> const parser<t> &nulparser(const t &);

/* Simple matching parser which matches a string and then returns a
 * fixed constant. */
template <typename t> const parser<t> &strmatcher(const char *, const t &);
/* Also specialised to void */
const parser<void> &strmatcher(const char *);

/* Dummy parser which always returns an error. */
template <typename t> const parser<t> &errparser(error);

namespace parsers {
/* Parser for the strings produced by fields::mk().escape().  Note
 * that the returned strings are in the temporary heap. */
extern const parser<const char *> &strparser;

/* Parser for integers produced by fields::mk() */
template <typename inttype> const parser<inttype> &intparser();
template <> const parser<unsigned long> &intparser();
template <> const parser<long> &intparser();

extern const parser<long double> &longdoubleparser;
}

/* Convenience functions. */
/* Match and ignore a string prefix */
template <typename t> const parser<t> &operator+(
    const char *prefix, const parser<t> &w);
/* Match and ignore a string suffix */
template <typename t> const parser<t> &operator+(
    const parser<t> &w, const char *suffix);
/* Like | but specialised for cases where the two alternatives have
   the same type and we don't care which we take. */
template <typename t> const parser<t> &operator||(
    const parser<t> &a, const parser<t> &b);

/* Parse a list of things separated by other things. */
namespace parsers {
template <typename a> const parser<list<a> > &
sepby(const parser<a> &what, const parser<void> &sep); }

/* Syntactic sugar for mapping over concatenations. */
namespace parsers {
template <typename o,
          typename a,
          typename b,
          typename c,
          typename d,
          typename e,
          typename f> const parser<o> &map(
              const ::parser<pair<pair<pair<pair<pair<a, b>, c>, d>, e>, f> > &in,
              const std::function<o (a, b, c, d, e, f)> &func); }

namespace parsers {
template <typename t> const parser<t> &defaultparser();
template <> const parser<int> &defaultparser<int>(); }

/* Test support */
namespace parsers {
template <typename t> void roundtrip(const parser<t> & = defaultparser<t>(),
                                     unsigned nr = 1000);
}

#endif /* !PARSERS_H__ */
