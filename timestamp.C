#include "timestamp.H"

#include <time.h>

#include "timedelta.H"

timestamp
timestamp::now()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return timestamp(ts.tv_sec * 1000000000ul + ts.tv_nsec);
}

timestamp
timestamp::operator+(timedelta td) const
{
    return timestamp(v + td.v);
}

timedelta
timestamp::operator-(timestamp o) const
{
    return timedelta(v - o.v);
}

bool
timestamp::operator<(const timestamp o) const {
    return v < o.v; }

struct timespec
timestamp::as_timespec() const {
    struct timespec res;
    res.tv_sec = v / 1000000000;
    res.tv_nsec = v % 1000000000;
    return res; }
