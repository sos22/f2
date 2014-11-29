#include "timedelta.H"

#include "parsers.H"
#include "quickcheck.H"
#include "fields.H"
#include "frequency.H"
#include "serialise.H"
#include "test.H"
#include "timestamp.H"

#include "parsers.tmpl"
#include "timedelta.tmpl"

timedelta::timedelta(const quickcheck &q, timedelta min, timedelta max)
    : v(min == max
        ? min.v
        : min.v + ((unsigned long)q % (max.v - min.v))) {}

timedelta::timedelta(const quickcheck &q)
    : v(q) {}

timedelta::timedelta(deserialise1 &ds)
    : v(ds) {}

timestamp
timedelta::operator+(timestamp ts) const
{
    return ts + *this;
}

double
timedelta::operator *(frequency f) const
{
    return (double)v * f.hz_ / 1e9;
}

const fields::field &
fields::mk(const timedelta &td) {
    return "<timedelta:" + fields::mk(td.v) + "ns>"; }

const parser<timedelta> &
parsers::_timedelta() {
    return ("<timedelta:" + intparser<long>() + "ns>")
        .map<timedelta>([] (long l) { return timedelta::nanoseconds(l); }); }

timestamp
timedelta::future() const { return timestamp::now() + *this; }

timedelta
timedelta::time(std::function<void ()> what) {
    auto start(timestamp::now());
    what();
    return timestamp::now() - start; }

void
timedelta::serialise(serialise1 &s) const { s.push(v); }

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
            assert(!(timedelta::seconds(1) >= timedelta::seconds(2)));
            assert(timedelta::seconds(1) * 2 == timedelta::seconds(2));
            assert(timedelta::seconds(2) / 2 == timedelta::seconds(1));
            assert(2 * timedelta::milliseconds(500) ==
                   timedelta::seconds(1)); });
    testcaseV("timedelta", "randrange", [] {
            for (unsigned x = 0; x < 1000; x++) {
                timedelta a((quickcheck()));
                timedelta b((quickcheck()));
                if (a < b) {
                    timedelta c(quickcheck(), a, b);
                    assert(a <= c);
                    assert(c < b); }
                else {
                    timedelta c(quickcheck(), b, a);
                    assert(b <= c);
                    assert(c <= a);
                    assert((c == a) == (a == b)); }
                assert(timedelta(quickcheck(), a, a) == a); } } );
    testcaseV("timedelta", "time", [] {
            auto t(timedelta::time<int>([] {
                        (timestamp::now()+timedelta::milliseconds(100))
                            .sleep(clientio::CLIENTIO);
                        return 5; }));
            assert(t.v == 5);
            assert(timedelta::milliseconds(100) < t.td);
            assert(t.td < timedelta::milliseconds(101)); });
    testcaseV("timedelta", "timeV", [] {
            auto t(timedelta::time([] {
                        (timestamp::now()+timedelta::milliseconds(100))
                            .sleep(clientio::CLIENTIO); }));
            assert(timedelta::milliseconds(100) < t);
            assert(t < timedelta::milliseconds(101)); }); }
