#include "timestamp.H"

#include <sys/time.h>
#include <time.h>

#include "clientio.H"
#include "fields.H"
#include "mutex.H"
#include "quickcheck.H"
#include "timedelta.H"
#include "util.H"

timestamp
timestamp::now()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return timestamp(ts.tv_sec * 1000000000l + ts.tv_nsec);
}

timestamp::timestamp(quickcheck q)
    : v(q) {}

timestamp
timestamp::operator+(timedelta td) const
{
    return timestamp(v + td.v);
}

timestamp
timestamp::operator-(timedelta td) const {
    return timestamp(v - td.v); }

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

void
timestamp::sleep(clientio) const {
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
