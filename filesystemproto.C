#include "filesystemproto.H"

#include "serialise.H"

const proto::filesystem::tag
proto::filesystem::tag::findjob(1);
const proto::filesystem::tag
proto::filesystem::tag::findstream(2);
const proto::filesystem::tag
proto::filesystem::tag::nominateagent(3);
const proto::filesystem::tag
proto::filesystem::tag::storagebarrier(4);

proto::filesystem::tag::tag(deserialise1 &ds)
    : proto::tag(ds) {
    if (*this != findjob &&
        *this != findstream &&
        *this != nominateagent &&
        *this != storagebarrier) {
        ds.fail(error::invalidmessage);
        *this = findjob; } }
