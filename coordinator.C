#include "coordinator.H"

#include "serialise.H"

const proto::coordinator::tag
proto::coordinator::tag::findjob(1);
const proto::coordinator::tag
proto::coordinator::tag::findstream(2);
const proto::coordinator::tag
proto::coordinator::tag::createjob(3);

proto::coordinator::tag::tag(deserialise1 &ds)
    : proto::tag(ds) {
    if (*this != findjob &&
        *this != findstream &&
        *this != createjob) {
        ds.fail(error::invalidmessage);
        *this = findjob; } }
