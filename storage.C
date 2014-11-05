#include "storage.H"

#include "error.H"
#include "serialise.H"

const proto::storage::tag
proto::storage::tag::createempty(91);
const proto::storage::tag
proto::storage::tag::append(92);
const proto::storage::tag
proto::storage::tag::finish(93);
const proto::storage::tag
proto::storage::tag::read(94);
const proto::storage::tag
proto::storage::tag::listjobs(95);
const proto::storage::tag
proto::storage::tag::liststreams(96);
const proto::storage::tag
proto::storage::tag::removestream(97);

proto::storage::tag::tag(unsigned x) : v(x) {}

proto::storage::tag::tag(deserialise1 &ds)
    : v(ds) {
    if (*this != createempty &&
        *this != append &&
        *this != finish &&
        *this != read &&
        *this != listjobs &&
        *this != liststreams &&
        *this != removestream) {
        ds.fail(error::invalidmessage);
        v = createempty.v; } }

void
proto::storage::tag::serialise(serialise1 &s) const { s.push(v); }
