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

wireproto_wrapper_type(peername)
