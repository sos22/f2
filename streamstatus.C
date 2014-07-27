#include "streamstatus.H"

#include "proto.H"

#include "wireproto.tmpl"

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

wireproto_wrapper_type(streamstatus);
void
streamstatus::addparam(wireproto::parameter<streamstatus> param,
                       wireproto::tx_message &txm) const {
    txm.addparam(wireproto::parameter<wireproto::tx_compoundparameter>(param),
                 wireproto::tx_compoundparameter()
                 .addparam(proto::streamstatus::name, name)
                 .addparam(proto::streamstatus::finished, finished_)
                 .addparam(proto::streamstatus::size, size)); }
maybe<streamstatus>
streamstatus::fromcompound(wireproto::rx_message const &rxm) {
    auto name(rxm.getparam(proto::streamstatus::name));
    auto finished(rxm.getparam(proto::streamstatus::finished));
    auto size(rxm.getparam(proto::streamstatus::size));
    if (!name || !finished || !size) return Nothing;
    return streamstatus(name.just(), finished.just(), size.just()); }
