#include "timestamp.H"

#include <sys/time.h>
#include <time.h>

#include "mutex.H"
#include "timedelta.H"
#include "util.H"

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

struct timeval
timestamp::as_timeval() const {
    /* Convert to the same clock as gettimeofday() uses. */
    static mutex_t baselock;
    static timestamp basetimestamp(timestamp::now());
    static timeval basetimeval;
    static bool havebase;
    if (!loadacquire(havebase)) {
        auto token(baselock.lock());
        if (!havebase) {
            basetimestamp = now();
            gettimeofday(&basetimeval, NULL);
            storerelease(&havebase, true); }
        baselock.unlock(&token); }
    long sincebasens = v - basetimestamp.v;
    timeval res(basetimeval);
    res.tv_sec += sincebasens / 1000000000;
    res.tv_usec += ((sincebasens % 1000000000) + 500000000) / 1000;
    while (res.tv_usec >= 1000000) {
        res.tv_sec++;
        res.tv_usec -= 1000000; }
    while (res.tv_usec < 0) {
        res.tv_sec--;
        res.tv_usec += 1000000; }
    return res; }
