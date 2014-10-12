/* Simple wrapper around double for represetning probabilities.
 * Only really useful for the extra documentation. */
#ifndef PROBABILITY_H__
#define PROBABILITY_H__

#include "wireproto.H"

template <typename> class orerror;
class quickcheck;

class probability;

namespace fields {
class field;
const field &mk(const probability); }

class probability {
    friend const fields::field &fields::mk(const probability);
private: probability() = delete;
private: double val; /* 0 to 1 */
public:  probability(const quickcheck &);
public:  static orerror<probability> mk(double);
public:  bool operator==(probability o) const {
    return val == o.val; }
public:  bool operator!=(probability o) const {
    return val != o.val; }
    /* Generate either true or false at random.  Probability of true
     * is the probability represented by this. */
public:  bool random() const;
public:  static const probability never;
public:  static const probability always;
    WIREPROTO_WRAPPER_TYPE(probability, double, private); };

#endif /* !PROBABILITY_H__ */
