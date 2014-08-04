#include "timestamp.H"

#include <sys/time.h>
#include <time.h>

#include "fields.H"
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

struct timespec
timestamp::as_timespec() const {
    struct timespec res;
    res.tv_sec = v / 1000000000;
    res.tv_nsec = v % 1000000000;
    return res; }

static mutex_t baselock;
static timestamp basetimestamp(timestamp::now());
static timeval basetimeval;
static bool havebase;

static void
inittimebase() {
    if (!loadacquire(havebase)) {
        auto token(baselock.lock());
        if (!havebase) {
            basetimestamp = timestamp::now();
            gettimeofday(&basetimeval, NULL);
            storerelease(&havebase, true); }
        baselock.unlock(&token); } }

struct timeval
timestamp::as_timeval() const {
    /* Convert to the same clock as gettimeofday() uses. */
    inittimebase();
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

timestamp
timestamp::fromtimeval(timeval tv) {
    inittimebase();
    tv.tv_sec -= basetimeval.tv_sec;
    tv.tv_usec -= basetimeval.tv_usec;
    return timestamp(tv.tv_sec * 1000000000ul + tv.tv_usec + basetimestamp.v); }

void
timestamp::sleep() const {
    while (1) {
        long left = v - now().v;
        if (left < 0) return;
        struct timespec ts;
        ts.tv_sec = left / 1000000000;
        ts.tv_nsec = left % 1000000000;
        nanosleep(&ts, NULL); } }

const fields::field &
fields::mk(const timestamp &ts) {
    static mutex_t basislock;
    static timestamp basis(0);
    static bool havebasis(false);
    if (!loadacquire(havebasis)) {
        auto token(basislock.lock());
        if (!havebasis) {
            basis = timestamp::now();
            storerelease(&havebasis, true); }
        basislock.unlock(&token); }
    return "<timestamp:" + mk(ts.v - basis.v) + "ns>";
}
