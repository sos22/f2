#include "frequency.H"

#include "fields.H"
#include "parsers.H"
#include "quickcheck.H"
#include "test.H"
#include "timedelta.H"

#include "parsers.tmpl"
#include "wireproto.tmpl"

wireproto_simple_wrapper_type(frequency, double, hz_)

frequency
frequency::hz(double hz)
{
    return frequency(hz);
}

frequency::frequency(const quickcheck &q) {
    do {
        hz_ = q;
    } while (hz_ <= 0); }

bool
frequency::operator==(frequency o) const {
    return (hz_ >= o.hz_ * 0.9999 && hz_ <= o.hz_ * 1.0001) ||
        (hz_ >= o.hz_ - 0.0001 && hz_ <= o.hz_ + 0.0001); }

frequency
frequency::operator+(frequency o) const {
    return frequency(hz_ + o.hz_); }

frequency
frequency::operator-(frequency o) const {
    return frequency(hz_ - o.hz_); }

frequency
frequency::operator*(double scale) const {
    return frequency(hz_ * scale); }

timedelta
operator/(double val, frequency f)
{
    return timedelta((long)(val / f.hz_ * 1e9 + .5));
}

const fields::field &
fields::mk(const frequency &f)
{
    return fields::mk_double(f.hz_) + "Hz";
}

const parser<frequency> &
parsers::_frequency() {
    return (parsers::doubleparser + "Hz")
        .map<frequency>([] (double d) { return frequency::hz(d); }); }

void
tests::_frequency() {
    testcaseV("frequency", "parser", [] {
            parsers::roundtrip(parsers::_frequency()); });
    testcaseV("frequency", "algebra", [] {
            assert(frequency::hz(5) + frequency::hz(7) == frequency::hz(12));
            assert(frequency::hz(5) - frequency::hz(3) == frequency::hz(2));
            assert(frequency::hz(5) * 2 == frequency::hz(10));
            assert(5.0/frequency::hz(10) == timedelta::milliseconds(500)); }); }
