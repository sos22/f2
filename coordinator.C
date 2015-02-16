#include "coordinator.H"

#include "serialise.H"

const proto::coordinator::tag
proto::coordinator::tag::findjob(1);
const proto::coordinator::tag
proto::coordinator::tag::findstream(2);
const proto::coordinator::tag
proto::coordinator::tag::createjob(3);

proto::coordinator::tag::tag(unsigned _v) : v(_v) {}
proto::coordinator::tag::tag(deserialise1 &ds)
    : v(ds.poprange(1, 3)) {}

void
proto::coordinator::tag::serialise(serialise1 &s) const {
    s.push(v); }
