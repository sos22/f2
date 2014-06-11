#include "timedelta.H"

#include "frequency.H"
#include "timestamp.H"

timestamp
timedelta::operator+(timestamp ts)
{
    return ts + *this;
}

double
timedelta::operator *(frequency f)
{
    return v * f.hz_ / 1e9;
}

timedelta
timedelta::seconds(long nr) {
    return nr * 1000000000l; }

long
timedelta::as_milliseconds() const {
    return (v + 500000) / 1000000; }

bool
timedelta::operator <(const timedelta &d) const {
    return v < d.v; }
