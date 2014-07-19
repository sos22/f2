#ifndef PARSERS_H__
#define PARSERS_H__

#include <functional>

#include "error.H"
#include "pair.H"
#include "tmpheap.H"

template <typename> class orerror;

extern tmpheap parserheap;

/* Class for things which can take strings and turn them into
 * rvals. */
template <typename rval>
class parser : public allocintmpheap<parserheap> {
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
public: orerror<rval> match(const char *) const;
};

/* Simpler type of parser which matches part of the string but doesn't
 * return anything. */
class strmatcher : public allocintmpheap<parserheap> {
private: const char *what;
public:  strmatcher(const char *_what) : what(_what) {}
public:  orerror<const char *> match(const char *buf) const;
public:  const strmatcher &operator+(const strmatcher &o) const;
};

/* Parser for the strings produced by fields::mk().escape() */
class strparser_ : public parser<const char *> {
public: orerror<result> parse(const char *) const;
};
extern const strparser_ strparser;

/* Parser for integers produced by fields::mk() */
extern const parser<long> &intparser;

/* Dummy parser which always returns an error. */
template <typename r>
class errparser : public parser<r> {
private: error e;
public:  errparser(error _e) : e(_e) {}
public:  orerror<typename parser<r>::result> parse(const char *) const; };

template <typename t> const parser<t> &operator+(
    const strmatcher &m,
    const parser<t> &p);
template <typename t> const parser<t> &operator+(
    const parser<t> &p,
    const strmatcher &m);
template <typename a, typename b> const parser<pair<a, b> > &operator+(
    const parser<a> &,
    const parser<b> &);

namespace tests {
void parsers(void);
}

#endif /* !PARSERS_H__ */
