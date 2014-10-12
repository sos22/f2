#include "probability.H"

#include <stdlib.h>

#include "fields.H"
#include "orerror.H"
#include "wireproto.H"

#include "wireproto.tmpl"

wireproto_simple_wrapper_type(probability, double, val)

const probability
probability::never(0);

const probability
probability::always(1);

probability::probability(const quickcheck &) {
    switch (random() % 100) {
    case 0 ... 10:
        val = 0;
        break;
    case 11 ... 20:
        val = 1;
        break;
    case 21 ... 30:
        val = 0.5;
        break;
    default:
        val = drand48(); } }

orerror<probability>
probability::mk(double d) {
    if (d < 0 || d > 1) return error::range;
    else return probability(d); }

bool
probability::random() const { return drand48() < val; }

const fields::field &
fields::mk(probability p) { return mk_double(p.val) + "%"; }
