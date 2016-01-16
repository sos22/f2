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

timestamp::tsfield::tsfield(long _v, bool _isdense) : v(_v), isdense(_isdense){}

const timestamp::tsfield &
timestamp::tsfield::dense() const { return *new tsfield(v, true); }

const timestamp::tsfield &
timestamp::tsfield::undense() const { return *new tsfield(v, false); }

void
timestamp::tsfield::fmt(fields::fieldbuf &buf) const {
    if (!isdense) buf.push("<timestamp:");
    fields::mk(v).fmt(buf);
    if (!isdense) buf.push("ns>"); }

const timestamp::tsfield &
timestamp::field() const {
    static mutex_t basislock;
    static timestamp basis(0);
    static racey<bool> havebasis(false);
    if (!havebasis.loadacquire()) {
        auto token(basislock.lock());
        if (!havebasis.load()) {
            basis = timestamp::now();
            havebasis.storerelease(true); }
        basislock.unlock(&token); }
    return *new tsfield(v - basis.v, false); }
