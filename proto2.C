#include "proto2.H"

#include "orerror.H"
#include "serialise.H"

#include "list.tmpl"

proto::sequencenr::sequencenr(deserialise1 &ds)
    : val(ds) {}

void
proto::sequencenr::serialise(serialise1 &s) const { s.push(val); }

proto::reqheader::reqheader(unsigned _size,
                            version _vers,
                            interfacetype _type,
                            sequencenr _seq)
    : size(_size),
      vers(_vers),
      type(_type),
      seq(_seq) {}

proto::reqheader::reqheader(deserialise1 &ds)
    : size(ds),
      vers(ds),
      type(ds),
      seq(ds) {}

void
proto::reqheader::serialise(serialise1 &s) {
    s.push(size);
    vers.serialise(s);
    type.serialise(s);
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

proto::hello::resp::resp(version _min,
                         version _max,
                         const list<interfacetype> &_type)
    : min(_min),
      max(_max),
      type(_type) {}

proto::hello::resp::resp(deserialise1 &ds)
    : min(ds),
      max(ds),
      type(ds) {}

void
proto::hello::resp::serialise(serialise1 &s) {
    min.serialise(s);
    max.serialise(s);
    type.serialise(s); }
