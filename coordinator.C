#include "coordinator.H"

#include "serialise.H"

const proto::coordinator::tag
proto::coordinator::tag::findjob(1);
const proto::coordinator::tag
proto::coordinator::tag::findstream(2);

proto::coordinator::tag::tag(unsigned _v) : v(_v) {}
proto::coordinator::tag::tag(deserialise1 &ds)
    : v(ds.poprange(1, 2)) {}

void
proto::coordinator::tag::serialise(serialise1 &s) const {
    s.push(v); }
