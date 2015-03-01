#include "filesystemproto.H"

#include "serialise.H"

const proto::filesystem::tag
proto::filesystem::tag::findjob(1);

proto::filesystem::tag::tag(deserialise1 &ds)
    : proto::tag(ds) {
    if (*this != findjob) {
        ds.fail(error::invalidmessage);
        *this = findjob; } }
