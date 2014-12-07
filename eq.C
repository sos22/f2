#include "eq.H"

#include "error.H"
#include "fields.H"
#include "serialise.H"

const proto::eq::name<unsigned>
proto::eq::names::testunsigned(1);

const proto::eq::tag
proto::eq::tag::subscribe(2);
const proto::eq::tag
proto::eq::tag::get(3);
const proto::eq::tag
proto::eq::tag::wait(4);
const proto::eq::tag
proto::eq::tag::trim(5);
const proto::eq::tag
proto::eq::tag::unsubscribe(6);

proto::eq::genname::genname(deserialise1 &ds) : v(ds) {}

void
proto::eq::genname::serialise(serialise1 &s) const { s.push(v); }

const fields::field &
proto::eq::genname::field() const { return "<eq:" + fields::mk(v) + ">"; }

proto::eq::subscriptionid::subscriptionid(deserialise1 &ds) : v(ds) {}

const fields::field &
proto::eq::subscriptionid::field() const {
    return "<sub:" + fields::mk(v) + ">"; }

void
proto::eq::subscriptionid::serialise(serialise1 &s) const { s.push(v); }

proto::eq::subscriptionid
proto::eq::subscriptionid::invent() {
    return subscriptionid((unsigned long)random() ^
                          ((unsigned long)random() << 22) ^
                          ((unsigned long)random() << 44)); }

proto::eq::eventid::eventid(deserialise1 &ds) : v(ds) {}

void
proto::eq::eventid::serialise(serialise1 &s) const { s.push(v); }

const fields::field &
proto::eq::eventid::field() const { return "<ev:" + fields::mk(v) + ">"; }

proto::eq::tag::tag(deserialise1 &ds)
    : v(ds) {
    if (*this != subscribe &&
        *this != get &&
        *this != wait &&
        *this != trim &&
        *this != unsubscribe) {
        ds.fail(error::invalidmessage); } }

void
proto::eq::tag::serialise(serialise1 &s) const { s.push(v); }