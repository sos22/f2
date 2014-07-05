#ifndef TIMEDELTA_H__
#define TIMEDELTA_H__

#include <functional>

class timestamp;
class frequency;

template <typename> struct timeres;

class timedelta {
    friend class timestamp;
    friend timedelta operator/(double, frequency);
    long v;
    timedelta(long _v) : v(_v) {}
public:
    static timedelta seconds(long nr) __attribute__((const));
    static timedelta milliseconds(long nr) __attribute__((const));
    static timedelta microseconds(long nr) __attribute__((const));
    timestamp operator+(timestamp);
    double operator*(frequency);

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
