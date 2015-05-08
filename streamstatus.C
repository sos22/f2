#include "streamstatus.H"

#include "fields.H"
#include "serialise.H"

const fields::field &
fields::mk(const streamstatus &sn) {
    return "<streamstatus: name=" + sn.name_.field() +
        " finished:" + mk(sn.finished_) +
        " size:" + sn.size.field() + ">"; }

streamstatus::streamstatus(const streamname &_streamname,
                           bool _finished,
                           bytecount _size)
    : name_(_streamname),
      finished_(_finished),
      size(_size) {}

streamstatus
streamstatus::empty(const streamname &sn) { return partial(sn, 0_B); }

streamstatus
streamstatus::partial(const streamname &sn, bytecount sz) {
    return streamstatus(sn, false, sz); }

streamstatus
streamstatus::finished(const streamname &sn, bytecount sz) {
    return streamstatus(sn, true, sz); }

bool
streamstatus::operator <(const streamstatus &sn) const {
    return name_ < sn.name_; }

bool
streamstatus::operator >(const streamstatus &sn) const {
    return name_ > sn.name_; }

streamstatus::streamstatus(deserialise1 &ds)
    : name_(ds),
      finished_(ds),
      size(ds) {}

void
streamstatus::serialise(serialise1 &s) const {
    name_.serialise(s);
    s.push(finished_);
    s.push(size); }
