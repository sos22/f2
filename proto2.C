#include "proto2.H"

#include "fields.H"
#include "orerror.H"
#include "serialise.H"

#include "list.tmpl"

const proto::meta::tag
proto::meta::tag::hello(1);
const proto::meta::tag
proto::meta::tag::abort(2);

proto::sequencenr::sequencenr(deserialise1 &ds)
    : val(ds) {}

void
proto::sequencenr::serialise(serialise1 &s) const { s.push(val); }

const fields::field &
proto::sequencenr::field() const {
    return "<seq:" + fields::mk(val) + ">"; }

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

proto::meta::tag::tag(deserialise1 &ds)
    : v(ds) {
    if (*this != hello && *this != abort) ds.fail(error::invalidmessage); }

void
proto::meta::tag::serialise(serialise1 &s) const { s.push(v); }
