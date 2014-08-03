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
    WIREPROTO_WRAPPER_TYPE(frequency, double, private);
public:

    static frequency hz(double);
    frequency(const quickcheck &);
    bool operator==(frequency) const;
    frequency operator+(frequency) const;
    frequency operator-(frequency) const;
    frequency operator*(double) const;
};

timedelta operator/(double, frequency);

template <typename> class parser;
namespace parsers {
const parser<frequency> &_frequency();
}

namespace tests {
void _frequency();
}

#endif /* !FREQUENCY_H__ */
