#include "version.H"

#include "serialise.H"

mktupledef(version)
const version version::current(1);

const version version::invalid(666);

version::version(deserialise1 &ds)
    : v(ds) {
    if (*this != current && *this != invalid) {
        *this = invalid;
        ds.fail(error::badversion); } }

void
version::serialise(serialise1 &s) const { s.push(v); }
