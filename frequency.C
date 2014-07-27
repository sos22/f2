#include "frequency.H"

#include "fields.H"
#include "quickcheck.H"
#include "timedelta.H"

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
    return hz_ == o.hz_; }

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
