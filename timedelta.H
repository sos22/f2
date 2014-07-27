#ifndef TIMEDELTA_H__
#define TIMEDELTA_H__

#include <functional>

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
    explicit timedelta(long _v) : v(_v) {}
public:
    static timedelta seconds(long nr) __attribute__((const));
    static timedelta milliseconds(long nr) __attribute__((const));
    static timedelta microseconds(long nr) __attribute__((const));
    timestamp operator+(timestamp);
    timedelta operator+(timedelta);
    double operator*(frequency);
    double operator/(timedelta);

    long as_milliseconds() const;
    bool operator<(const timedelta &o) const;

    template <typename t> static timeres<t> time(std::function<t ()>);
};

template <typename t> struct timeres {
public: class timedelta td;
public: t v;
public: timeres(const timedelta &_td, const t &_v)
    : td(_td), v(_v) {}
public: operator t&() { return v; }
public: operator class timedelta &() { return td; }
};

#endif /* !TIMEDELTA_H__ */
