#include "streamstatus.H"

#include "fields.H"
#include "serialise.H"

streamstatus::streamstatus(const streamname &_streamname,
                           bool _finished,
                           bytecount _size)
    : name_(_streamname),
      finished_(_finished),
      size_(_size) {}

streamstatus
streamstatus::empty(const streamname &sn) { return partial(sn, 0_B); }

streamstatus
streamstatus::partial(const streamname &sn, bytecount sz) {
    return streamstatus(sn, false, sz); }

streamstatus
streamstatus::finished(const streamname &sn, bytecount sz) {
    return streamstatus(sn, true, sz); }

bool
streamstatus::operator==(const streamstatus &o) const {
    return name_ == o.name_ &&
        finished_ == o.finished_ &&
        size_ == o.size_; }

streamstatus::streamstatus(deserialise1 &ds)
    : name_(ds),
      finished_(ds),
      size_(ds) {}

void
streamstatus::serialise(serialise1 &s) const {
    s.push(name_);
    s.push(finished_);
    s.push(size_); }

const fields::field &
streamstatus::field() const {
    return "<streamstatus: name=" + name_.field() +
        " finished:" + fields::mk(finished_) +
        " size:" + size().field() + ">"; }
