#include "streamstatus.H"

#include "fields.H"
#include "serialise.H"

const fields::field &
fields::mk(const streamstatus &sn) {
    return "<streamstatus: name=" + mk(sn.name) +
        " finished:" + mk(sn.finished_) +
        " size:" + mk(sn.size) + ">"; }

streamstatus::streamstatus(const streamname &_streamname,
                           bool _finished,
                           uint64_t _size)
    : name(_streamname),
      finished_(_finished),
      size(_size) {}

streamstatus
streamstatus::partial(const streamname &sn, uint64_t sz) {
    return streamstatus(sn, false, sz); }

streamstatus
streamstatus::finished(const streamname &sn, uint64_t sz) {
    return streamstatus(sn, true, sz); }

bool
streamstatus::operator <(const streamstatus &sn) const {
    return name < sn.name; }

bool
streamstatus::operator >(const streamstatus &sn) const {
    return name > sn.name; }

streamstatus::streamstatus(deserialise1 &ds)
    : name(ds),
      finished_(ds),
      size(ds) {}

void
streamstatus::serialise(serialise1 &s) const {
    name.serialise(s);
    s.push(finished_);
    s.push(size); }
