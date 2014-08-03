#include "timedelta.H"

#include "fields.H"
#include "frequency.H"
#include "timestamp.H"

timestamp
timedelta::operator+(timestamp ts)
{
    return ts + *this;
}

timedelta
timedelta::operator+(timedelta d) {
    return timedelta(v + d.v); }

double
timedelta::operator *(frequency f)
{
    return (double)v * f.hz_ / 1e9;
}

double
timedelta::operator /(timedelta o) {
    return (double)v / (double)o.v; }

bool
timedelta::operator ==(timedelta o) {
    return v == o.v; }

timedelta
timedelta::seconds(long nr) {
    return timedelta(nr * 1000000000l); }

timedelta
timedelta::milliseconds(long nr) {
    return timedelta(nr * 1000000l); }

timedelta
timedelta::microseconds(long nr) {
    return timedelta(nr * 1000l); }

long
timedelta::as_milliseconds() const {
    return (v + 500000) / 1000000; }

bool
timedelta::operator <(const timedelta &d) const {
    return v < d.v; }

const fields::field &
fields::mk(const timedelta &td) {
    return "<timedelta:" + fields::mk(td.v) + "ns>"; }
