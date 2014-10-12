#ifndef TIMEDELTA_H__
#define TIMEDELTA_H__

#include <functional>

#include "wireproto.H"

class quickcheck;
class timedelta;
class timestamp;
class frequency;

template <typename> struct timeres;

namespace fields {
class field;
const field &mk(const timedelta &);
}

class timedelta {
    friend const fields::field &fields::mk(const timedelta &);
    friend class timestamp;
    friend timedelta operator/(double, frequency);
    long v;
public:
    /* Random over entire possible space */
    timedelta(const quickcheck &);
    /* Random over a limited range. */
    timedelta(const quickcheck &, timedelta min, timedelta max);
    static timedelta seconds(long nr) { return timedelta(nr * 1000000000l); }
    static timedelta milliseconds(long nr) { return timedelta(nr * 1000000l); }
    static timedelta microseconds(long nr) { return timedelta(nr * 1000l); }
    static timedelta nanoseconds(long nr) { return timedelta(nr); }
    timestamp operator+(timestamp) const;
    timedelta operator+(timedelta d) const { return timedelta(v+d.v); }
    timedelta operator-(timedelta d) const { return timedelta(v-d.v); }
    double operator*(frequency) const;
    timedelta operator*(double d) const {
        return timedelta((long)((double)v * d)); }
    double operator/(timedelta d) const { return (double)v/(double)d.v; }
    timedelta operator/(double d) const { return *this * (1.0/d); }
    bool operator>(timedelta o) const { return v > o.v; }
    bool operator>=(timedelta o) const { return v >= o.v; }
    bool operator==(timedelta o) const { return v == o.v; }
    bool operator<=(timedelta o) const { return v <= o.v; }
    bool operator<(timedelta o) const { return v < o.v; }

    long as_milliseconds() const { return (v + 500000) / 1000000; }

    template <typename t> static timeres<t> time(std::function<t ()>);
    static timedelta time(std::function<void ()>);

    WIREPROTO_WRAPPER_TYPE(timedelta, long, private);
};

static inline timedelta operator*(double scale, timedelta v) {
    return v * scale; }

template <typename t> class timeres {
public: class timedelta td;
public: t v;
public: timeres(const timedelta &_td, const t &_v)
    : td(_td), v(_v) {}
public: operator t&() { return v; }
public: operator class timedelta &() { return td; }
};

template <typename> class parser;
namespace parsers {
const parser<timedelta> &_timedelta();
}

namespace tests {
void _timedelta();
}

#endif /* !TIMEDELTA_H__ */
