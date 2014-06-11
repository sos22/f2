#include "frequency.H"

#include "fields.H"
#include "timedelta.H"

frequency
frequency::hz(double hz)
{
    return frequency(hz);
}

frequency::frequency(double _hz)
    : hz_(_hz)
{}

timedelta
operator/(double val, frequency f)
{
    return timedelta(val / f.hz_ * 1e9);
}

const fields::field &
fields::mk(const frequency &f)
{
    return fields::mk_double(f.hz_) + "Hz";
}
