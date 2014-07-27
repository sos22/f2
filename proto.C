#include "proto.H"

#include "fields.H"
#include "frequency.H"
#include "logging.H"
#include "mastersecret.H"
#include "nonce.H"
#include "ratelimiter.H"
#include "registrationsecret.H"
#include "peername.H"
#include "shutdown.H"
#include "wireproto.H"
#include "wireproto.tmpl"

wireproto_simple_wrapper_type(digest, unsigned long, val)
wireproto_simple_wrapper_type(frequency, double, hz_)
wireproto_simple_wrapper_type(masternonce, digest, d);
wireproto_simple_wrapper_type(memlog_idx, unsigned long, val)
wireproto_simple_wrapper_type(nonce, uint64_t, val);
wireproto_simple_wrapper_type(registrationsecret, string, secret);
wireproto_simple_wrapper_type(shutdowncode, int, code)

wireproto_wrapper_type(memlog_entry)
namespace wireproto {
    template tx_message &tx_message::addparam(
        parameter<list<memlog_entry> >, const list<memlog_entry> &);
    template orerror<void> rx_message::fetch(
        parameter<list<memlog_entry> >,
        list<memlog_entry> &) const;
};
void
memlog_entry::addparam(wireproto::parameter<memlog_entry> tmpl,
                       wireproto::tx_message &tx_msg) const
{
    const char *_msg = this->msg;
    tx_msg.addparam(wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
                    wireproto::tx_compoundparameter().
                    addparam(proto::memlog_entry::msg, _msg).
                    addparam(proto::memlog_entry::idx, idx));
}
maybe<memlog_entry>
memlog_entry::fromcompound(const wireproto::rx_message &p)
{
    auto msg_(p.getparam(proto::memlog_entry::msg));
    auto idx(p.getparam(proto::memlog_entry::idx));
    if (msg_ == Nothing || idx == Nothing)
        return Nothing;
    return memlog_entry(idx.just(), msg_.just());
}

wireproto_wrapper_type(ratelimiter_status)
void
ratelimiter_status::addparam(wireproto::parameter<ratelimiter_status> tmpl,
                             wireproto::tx_message &tx_msg) const
{
    tx_msg.addparam(wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
                    wireproto::tx_compoundparameter()
                    .addparam(proto::ratelimiter_status::max_rate, max_rate)
                    .addparam(proto::ratelimiter_status::bucket_size,
                              bucket_size)
                    .addparam(proto::ratelimiter_status::bucket_content,
                              bucket_content)
                    .addparam(proto::ratelimiter_status::dropped,
                              dropped));
}
maybe<ratelimiter_status>
ratelimiter_status::fromcompound(const wireproto::rx_message &msg)
{
    auto max_rate(msg.getparam(proto::ratelimiter_status::max_rate));
    auto bucket_size(msg.getparam(proto::ratelimiter_status::bucket_size));
    auto bucket_content(
        msg.getparam(proto::ratelimiter_status::bucket_content));
    auto dropped(msg.getparam(proto::ratelimiter_status::dropped));
    if (!max_rate || !bucket_size || !bucket_content || !dropped)
        return Nothing;
    return ratelimiter_status(max_rate.just(),
                              bucket_size.just(),
                              bucket_content.just(),
                              dropped.just());
}

wireproto_wrapper_type(peername)

namespace wireproto {
template <> tx_message &
tx_message::addparam(
    parameter<timeval> tmpl,
    const timeval &tv) {
    return addparam(parameter<tx_compoundparameter>(tmpl),
                    tx_compoundparameter()
                    .addparam(proto::timeval::tv_sec, tv.tv_sec)
                    .addparam(proto::timeval::tv_usec, tv.tv_usec)); }
template <> maybe<timeval>
rx_message::getparam(parameter<timeval> tmpl) const {
    auto packed(getparam(parameter<rx_message>(tmpl)));
    if (!packed) return Nothing;
    auto tv_sec(packed.just().getparam(proto::timeval::tv_sec));
    auto tv_usec(packed.just().getparam(proto::timeval::tv_usec));
    if (!tv_sec || !tv_usec) return Nothing;
    timeval res;
    res.tv_sec = tv_sec.just();
    res.tv_usec = tv_usec.just();
    return res; }
template <> maybe<timeval>
deserialise(bufslice &slice) {
    auto packed(deserialise<rx_message>(slice));
    if (!packed) return Nothing;
    auto tv_sec(packed.just().getparam(proto::timeval::tv_sec));
    auto tv_usec(packed.just().getparam(proto::timeval::tv_usec));
    if (!tv_sec || !tv_usec) return Nothing;
    timeval res;
    res.tv_sec = tv_sec.just();
    res.tv_usec = tv_usec.just();
    return res; } }
