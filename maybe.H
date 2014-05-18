#ifndef MAYBE_H__
#define MAYBE_H__

#include <assert.h>

template <typename t>
class maybe {
    bool r;
    unsigned char content[sizeof(t)];
    maybe()
	: r(true)
	{
	}
public:
    maybe(const t &what)
	: r(false)
	{
	    new (content) t(what);
	}
    static maybe<t> mkjust(const t &what)
	{
	    return maybe<t>(what);
	}
    static maybe<t> mknothing()
	{
	    return maybe<t>();
	}
    bool isnothing() const { return r; }
    bool isjust() const { return !r; }
    const t &just() const {
	assert(isjust());
	return *(t *)content;
    }

    maybe<t> operator||(const maybe<t> &o) const {
	if (isjust()) return *this;
	else return o;
    }
};

#endif /* !MAYBE_H__ */
