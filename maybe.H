#ifndef MAYBE_H__
#define MAYBE_H__

#include <algorithm>
#include <functional>

#include <assert.h>
#include <string.h>

#include "error.H"

class deserialise1;
class quickcheck;
class serialise1;

template <typename> class maybe;
class _Nothing {
public: template <typename t> bool operator==(const maybe<t> &) const;
public: template <typename t> bool operator!=(const maybe<t> &) const;
public: _Nothing() {}
};
const class _Nothing Nothing;

template <typename t>
class maybe {
    bool r;
    unsigned char content[sizeof(t)];
    maybe() = delete;
public:
    maybe(deserialise1 &);
    maybe(const quickcheck &);
    maybe(const maybe &o)
        : r(o.r)
        {
            if (!r)
                new (content) t(o.just());
        }
    maybe(maybe &&o)
        : r(o.r)
        {
            if (!r)
                new (content) t(std::move(o.just()));
        }
    ~maybe()
        {
            if (isjust()) ((t *)content)->~t();
        }
    maybe(_Nothing)
        : r(true)
        {
            /* Shut the compiler up */
            memset(content, 0xbb, sizeof(content));
        }
    maybe(const t &what)
        : r(false)
        {
            ::new (content) t(what);
        }
    maybe(t &&what)
        : r(false)
        {
            ::new (content) t(what);
        }
    template <typename ... args> void mkjust(args &&... params);
    void operator=(const maybe &o)
        {
            if (isjust())
                just().~t();
            r = o.r;
            if (isjust())
                new (content) t(o.just());
        }
    void operator=(const t &o) {
        if (isjust()) just().~t();
        r = false;
        new (content) t(o); }
    bool isnothing() const { return r; }
    bool operator!() const { return isnothing(); }
    bool isjust() const { return !r; }
    const t &just() const {
        assert(isjust());
        return *(t *)content;
    }
    t &just() {
        assert(isjust());
        return *(t *)content;
    }
    const t &dflt(const t &what) const {
        if (isjust()) return just();
        else return what;
    }
#ifndef COVERAGESKIP
    const t &fatal(const fields::field &msg) const {
        if (isjust()) return just();
        else error::nothing.fatal(msg); }
    const t &fatal(const char *msg) const {
        if (isjust()) return just();
        else error::nothing.fatal(msg); }
#endif
    bool operator==(const maybe &o) const {
        if (isjust() != o.isjust()) return false;
        else return isnothing() || just() == o.just(); }
    bool operator!=(const maybe &o) const {
        return !(*this == o); }
    bool operator==(const _Nothing) const {
        return isnothing();
    }
    bool operator!=(const _Nothing) const {
        return isjust();
    }
    bool operator==(const t &o) const {
        return isjust() && just() == o;
    }
    bool operator!=(const t &o) const {
        return isnothing() || just() != o;
    }

    template <typename o> maybe<o> map(const std::function<o (const t&)> &f) {
        if (isnothing()) return Nothing;
        else return f(just()); }

    void serialise(serialise1 &) const;
};
template <typename t> maybe<t> mkjust(const t &);

/* Special case for maybe<void> */
template <> class maybe<void> {
private: bool r;
private: maybe() : r(false) {}
public:  maybe(const quickcheck &);
public:  template <typename t> maybe(const maybe<t> &o)
    : r(o.r) {}
public:  maybe(_Nothing)
    : r(true) {}
public:  static maybe just;
public:  void operator=(const maybe &o) {
    r = o.r; }
public:   bool isnothing() const { return r; }
public:   bool operator!() const { return isnothing(); }
public:   bool isjust() const { return !r; }
public:   bool operator==(const _Nothing) const {
    return isnothing(); }
public:   bool operator!=(const _Nothing) const {
    return isjust(); }
public:   bool operator==(const maybe<void> &x) const {
    return isjust() == x.isjust(); }
public:   bool operator!=(const maybe<void> &x) const {
    return !(*this == x); }
public:   template <typename o> maybe<o> map(const std::function<o ()> &f) {
        if (isnothing()) return Nothing;
        else return f(); } };

template <typename t> bool
_Nothing::operator==(const maybe<t> &o) const {
    return o.isnothing(); }

template <typename t> bool
_Nothing::operator!=(const maybe<t> &o) const {
    return o.isjust(); }

template <typename t> bool
operator ==(const t &a, const maybe<t> &o) {
    return o.isjust() && a == o.just(); }

template <typename t> bool
operator !=(const t &a, const maybe<t> &o) {
    return o.isnothing() || a != o.just(); }

template <typename> class parser;
namespace parsers {
template <typename t> const parser<maybe<t> > &_maybe(const parser<t> &);
}

namespace tests {
void _maybe();
}

#endif /* !MAYBE_H__ */
