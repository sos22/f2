#ifndef PARSERS_H__
#define PARSERS_H__

#include <functional>

#include "error.H"
#include "pair.H"
#include "tmpheap.H"

template <typename, typename> class either;
class filename;
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
};

/* Special case for parsers which don't return anything. */
template <>
class parser<void> : public tmpheap::base {
public: typedef const char *result;
public: virtual orerror<const char *> parse(const char *) const = 0;
public: maybe<error> match(const string &) const;
/* Concatenation and alternation have simpler types here. */
public: template <typename t> const parser<t> &operator+(
    const parser<t> &) const;
public: template <typename t> const parser<maybe<t> > &operator|(
    const parser<t> &) const;
public: const parser<void> &operator+(const parser<void> &) const;
public: const parser<void> &operator|(const parser<void> &) const;
};

/* Simple matching parser which matches a string and then returns a
 * fixed constant. */
template <typename t> const parser<t> &strmatcher(const char *, const t &);
/* Also specialised to void */
const parser<void> &strmatcher(const char *);

/* Dummy parser which always returns an error. */
template <typename t> const parser<t> &errparser(error);

/* Parser for the strings produced by fields::mk().escape().  Note
 * that the returned strings are in the temporary heap. */
extern const parser<const char *> &strparser;

/* Parser for integers produced by fields::mk() */
extern const parser<long> &intparser;

/* Convenience functions. */
template <typename t> const parser<t> &operator+(
    const char *prefix, const parser<t> &w);
template <typename t> const parser<t> &operator+(
    const parser<t> &w, const char *suffix);

namespace tests {
void parsers(void);
}

#endif /* !PARSERS_H__ */
