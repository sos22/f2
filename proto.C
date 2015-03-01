#include "proto.H"

#include "serialise.H"

proto::tag::tag(deserialise1 &ds) : d(ds) {}

void
proto::tag::serialise(serialise1 &s) const { s.push(d); }
