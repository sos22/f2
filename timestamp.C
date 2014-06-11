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
