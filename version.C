#include "version.H"

#include "fields.H"
#include "serialise.H"

const version version::current(1);
const version version::invalid(666);

/* We deliberately don't fail deserialise for invalid versions so that
 * we don't get in the way of upgrade handling. */
version::version(deserialise1 &ds) : v(ds) { }

void
version::serialise(serialise1 &s) const { s.push(v); }

const fields::field &
version::field() const { return "v"+fields::mk(v); }
