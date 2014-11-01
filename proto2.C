#include "proto2.H"

#include "orerror.H"
#include "serialise.H"

proto::sequencenr::sequencenr(deserialise1 &ds)
    : val(ds) {}

void
proto::sequencenr::serialise(serialise1 &s) const { s.push(val); }

proto::reqheader::reqheader(unsigned _size, version _vers, sequencenr _seq)
    : size(_size),
      vers(_vers),
      seq(_seq) {}

proto::reqheader::reqheader(deserialise1 &ds)
    : size(ds),
      vers(ds),
      seq(ds) {}

void
proto::reqheader::serialise(serialise1 &s) {
    s.push(size);
    vers.serialise(s);
    seq.serialise(s); }

proto::respheader::respheader(unsigned _size,
                              sequencenr _seq,
                              orerror<void> _status)
    : size(_size),
      seq(_seq),
      status(_status) {}

proto::respheader::respheader(deserialise1 &ds)
    : size(ds),
      seq(ds),
      status(ds) {}

void
proto::respheader::serialise(serialise1 &s) {
    s.push(size);
    seq.serialise(s);
    status.serialise(s); }

proto::hello::resp::resp(version _min, version _max)
    : min(_min),
      max(_max) {}

proto::hello::resp::resp(deserialise1 &ds)
    : min(ds),
      max(ds) {}

void
proto::hello::resp::serialise(serialise1 &s) {
    min.serialise(s);
    max.serialise(s); }
