#ifndef MAYBE_H__
#define MAYBE_H__

#include <assert.h>

#include "obj.H"

class _Nothing {
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
    ~maybe()
        {
            if (isjust()) ((t *)content)->~t();
        }
    maybe(_Nothing)
        : r(true)
        {
        }
    maybe(const t &what)
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
};

#endif /* !MAYBE_H__ */
