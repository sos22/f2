#ifndef TIMEDELTA_H__
#define TIMEDELTA_H__

class timestamp;
class frequency;

class timedelta {
    friend class timestamp;
    friend timedelta operator/(double, frequency);
    long v;
    timedelta(long _v) : v(_v) {}
public:
    static timedelta seconds(long nr);
    static timedelta milliseconds(long nr);
    timestamp operator+(timestamp);
    double operator*(frequency);

    long as_milliseconds() const;
    bool operator<(const timedelta &o) const;
};

#endif /* !TIMEDELTA_H__ */
