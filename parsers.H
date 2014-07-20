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
template <typename rval> class parser;
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
    operator + (const parser<other> &o);
    /* Parser alternation.  Note that this has no look-ahead, so once
       something's matched we won't backtrack for later failures. */
public: template <typename other> const parser< either<rval, other> > &
    operator | (const parser<other> &o);
};

/* Special case for parsers which don't return anything. */
template <>
class parser<void> : public tmpheap::base {
public: virtual orerror<const char *> parse(const char *) const = 0;
public: orerror<const char *> match(const string &) const;
/* Concatenation and alternation have simpler types here. */
public: template <typename t> const parser<t> &operator+(
    const parser<t> &) const;
public: template <typename t> const parser<maybe<t> > &operator|(
    const parser<t> &) const;
};

/* Simpler type of parser which matches part of the string and returns
 * a constant. */
template <typename t>
class strmatcher : public parser<t> {
private: const char *what;
private: const t val;
public:  strmatcher(const char *_what, const t &_val)
    : what(_what), val(_val) {}
public:  orerror<typename parser<t>::result> parse(const char *) const;
};

/* Also specialised to void */
template <>
class strmatcher<void> : public parser<void> {
private: const char *what;
public:  strmatcher(const char *_what)
    : what(_what) {}
public:  orerror<const char *> parse(const char *) const;
};

/* Dummy parser which always returns an error. */
template <typename r>
class errparser : public parser<r> {
private: error e;
public:  errparser(error _e) : e(_e) {}
public:  orerror<typename parser<r>::result> parse(const char *) const; };

/* Parser for the strings produced by fields::mk().escape().  Note
 * that the returned strings are in the temporary heap. */
extern const parser<const char *> &strparser;

/* Parser for integers produced by fields::mk() */
extern const parser<long> &intparser;

namespace tests {
void parsers(void);
}

#endif /* !PARSERS_H__ */
