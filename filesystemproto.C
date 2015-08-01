#include "filesystemproto.H"

#include "serialise.H"

const proto::filesystem::tag
proto::filesystem::tag::ping(1);
const proto::filesystem::tag
proto::filesystem::tag::findjob(2);
const proto::filesystem::tag
proto::filesystem::tag::findstream(3);
const proto::filesystem::tag
proto::filesystem::tag::nominateagent(4);
const proto::filesystem::tag
proto::filesystem::tag::storagebarrier(5);

proto::filesystem::tag::tag(deserialise1 &ds)
    : proto::tag(ds) {
    if (*this != ping &&
        *this != findjob &&
        *this != findstream &&
        *this != nominateagent &&
        *this != storagebarrier) {
        ds.fail(error::invalidmessage);
        *this = findjob; } }
