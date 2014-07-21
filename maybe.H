#ifndef MAYBE_H__
#define MAYBE_H__

#include <algorithm>

#include <assert.h>
#include <string.h>

#include "obj.H"

template <typename> class maybe;
class _Nothing {
public: template <typename t> bool operator==(const maybe<t> &) const;
public: template <typename t> bool operator!=(const maybe<t> &) const;
};
const class _Nothing Nothing;

template <typename t>
class maybe {
    bool r;
    unsigned char content[sizeof(t)];
    maybe() = delete;
public:
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
    void operator=(const maybe &o)
        {
            if (isjust())
                just().~t();
            r = o.r;
            if (isjust())
                new (content) t(o.just());
        }
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
};

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

#endif /* !MAYBE_H__ */
