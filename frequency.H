#ifndef FREQUENCY_H__
#define FREQUENCY_H__

#include "wireproto.H"

class quickcheck;
class timedelta;
class frequency;

namespace fields {
class field;
const field &mk(const frequency &);
}

class frequency {
    friend class timedelta;
    friend timedelta operator/(double, frequency);
    friend const fields::field &fields::mk(const frequency &);
    double hz_;
    explicit frequency(double);
    double unwrap() const { return hz_; }
public:
    WIREPROTO_WRAPPER_TYPE(frequency, double);

    static frequency hz(double);
    frequency(const quickcheck &);
    bool operator==(const frequency &) const;
};

timedelta operator/(double, frequency);

#endif /* !FREQUENCY_H__ */
