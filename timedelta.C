#include "timedelta.H"

#include "parsers.H"
#include "quickcheck.H"
#include "fields.H"
#include "frequency.H"
#include "test.H"
#include "timestamp.H"

#include "parsers.tmpl"
#include "timedelta.tmpl"
#include "wireproto.tmpl"

timedelta::timedelta(const quickcheck &q)
    : v(q) {}

timestamp
timedelta::operator+(timestamp ts) const
{
    return ts + *this;
}

timedelta
timedelta::operator+(timedelta d) const {
    return timedelta(v + d.v); }

timedelta
timedelta::operator-(timedelta d) const {
    return timedelta(v - d.v); }

double
timedelta::operator *(frequency f) const
{
    return (double)v * f.hz_ / 1e9;
}

double
timedelta::operator /(timedelta o) const {
    return (double)v / (double)o.v; }

bool
timedelta::operator <=(timedelta o) const {
    return v <= o.v; }

bool
timedelta::operator ==(timedelta o) const {
    return v == o.v; }

bool
timedelta::operator >=(timedelta o) const {
    return v >= o.v; }

timedelta
timedelta::seconds(long nr) {
    return timedelta(nr * 1000000000l); }

timedelta
timedelta::milliseconds(long nr) {
    return timedelta(nr * 1000000l); }

timedelta
timedelta::microseconds(long nr) {
    return timedelta(nr * 1000l); }

timedelta
timedelta::nanoseconds(long nr) {
    return timedelta(nr); }

long
timedelta::as_milliseconds() const {
    return (v + 500000) / 1000000; }

bool
timedelta::operator <(const timedelta &d) const {
    return v < d.v; }

const fields::field &
fields::mk(const timedelta &td) {
    return "<timedelta:" + fields::mk(td.v) + "ns>"; }

const parser<timedelta> &
parsers::_timedelta() {
    return ("<timedelta:" + intparser<long>() + "ns>")
        .map<timedelta>([] (long l) { return timedelta::nanoseconds(l); }); }

wireproto_simple_wrapper_type(timedelta, long, v)

void
tests::_timedelta() {
    testcaseV("timedelta", "parsers",
              [] { parsers::roundtrip(parsers::_timedelta()); });
    testcaseV("timedelta", "algebra", [] {
            assert(timedelta::milliseconds(200) + timedelta::milliseconds(800)==
                   timedelta::seconds(1));
            timestamp n(timestamp::now());
            assert(timedelta::seconds(1) + n ==
                   n +
                   timedelta::milliseconds(100) +
                   timedelta::microseconds(800000) +
                   timedelta::nanoseconds(100000000));
            assert(timedelta::milliseconds(500) * frequency::hz(2) == 1);
            assert(timedelta::seconds(1) / timedelta::milliseconds(10) == 100);
            assert(timedelta::milliseconds(100).as_milliseconds() == 100);
            assert(timedelta::milliseconds(5) < timedelta::milliseconds(10));
            assert(timedelta::seconds(1) - timedelta::milliseconds(1) ==
                   timedelta::milliseconds(999));
            assert(timedelta::seconds(1) <= timedelta::seconds(1));
            assert(timedelta::seconds(1) <= timedelta::seconds(2));
            assert(!(timedelta::seconds(2) <= timedelta::seconds(1)));
            assert(timedelta::seconds(1) >= timedelta::seconds(1));
            assert(timedelta::seconds(2) >= timedelta::seconds(1));
            assert(!(timedelta::seconds(1) >= timedelta::seconds(2))); });
    testcaseV("timedelta", "time", [] {
            auto t(timedelta::time<int>([] {
                        (timestamp::now()+timedelta::milliseconds(100))
                            .sleep();
                        return 5; }));
            assert(t.v == 5);
            assert(timedelta::milliseconds(100) < t.td);
            assert(t.td < timedelta::milliseconds(101)); }); }
