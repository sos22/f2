#ifndef TIMEDELTA_H__
#define TIMEDELTA_H__

#include <functional>

class deserialise1;
namespace fields { class field; }
class quickcheck;
class serialise1;
class timedelta;
class timestamp;

template <typename> struct timeres;
template <typename> class parser;

class timedelta {
    friend class timestamp;
    long v;
    explicit timedelta(long _v) : v(_v) {}
public:
    /* Random over entire possible space */
    timedelta(const quickcheck &);
    /* Random over a limited range. */
    timedelta(const quickcheck &, timedelta min, timedelta max);
    timedelta(deserialise1 &);
    static timedelta weeks(long nr) { return days(nr * 7l); }
    static timedelta days(long nr) { return hours(nr * 24l); }
    static timedelta hours(long nr) { return minutes(nr * 60l); }
    static timedelta minutes(long nr) { return seconds(nr * 60l); }
    static timedelta seconds(long nr) { return milliseconds(nr * 1000l); }
    static timedelta milliseconds(long nr) { return microseconds(nr * 1000l); }
    static timedelta microseconds(long nr) { return nanoseconds(nr * 1000l); }
    static timedelta nanoseconds(long nr) { return timedelta(nr); }
    timestamp operator+(timestamp) const;
    timedelta operator+(timedelta d) const { return timedelta(v+d.v); }
    timedelta operator-(timedelta d) const { return timedelta(v-d.v); }
    timedelta operator-() const { return timedelta(-v); }
    timedelta operator*(double d) const {
        return timedelta((long)((double)v * d)); }
    double operator/(timedelta d) const { return (double)v/(double)d.v; }
    timedelta operator/(double d) const { return *this * (1.0/d); }
    bool operator>(timedelta o) const { return v > o.v; }
    bool operator>=(timedelta o) const { return v >= o.v; }
    bool operator==(timedelta o) const { return v == o.v; }
    bool operator!=(timedelta o) const { return v != o.v; }
    bool operator<=(timedelta o) const { return v <= o.v; }
    bool operator<(timedelta o) const { return v < o.v; }

    timestamp future() const;

    long as_milliseconds() const { return (v + 500000) / 1000000; }
    long as_nanoseconds() const { return v; }
    timespec astimespec() const;

    template <typename t> static timeres<t> time(std::function<t ()>);
    static timedelta time(std::function<void ()>);

    const fields::field &field() const;
    void serialise(serialise1 &) const;
public: static const ::parser<timedelta> &parser();
};

static inline timedelta
operator "" _ms(long double inner) {
    return timedelta::nanoseconds((long)(inner * 1000000)); }

static inline timedelta
operator "" _ms(unsigned long long inner) {
    return timedelta::milliseconds((long)inner); }

static inline timedelta
operator "" _s(long double inner) {
    return timedelta::nanoseconds((long)(inner * 1000000000)); }

static inline timedelta
operator "" _s(unsigned long long inner) {
    return timedelta::seconds((long)inner); }

static inline timedelta operator*(double scale, timedelta v) {
    return v * scale; }

template <typename t> class timeres {
private: timeres() = delete;
public: class timedelta td;
public: t v;
public: timeres(const timedelta &_td, const t &_v)
    : td(_td), v(_v) {}
public: operator t&() { return v; }
public: operator class timedelta &() { return td; }
    /* If we don't have a destructor then we get a coverage failure.
     * I'm not sure why. */
public: ~timeres() {}
};

namespace tests {
void _timedelta();
}

#endif /* !TIMEDELTA_H__ */
