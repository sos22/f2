#include "proto.H"

#include "logging.H"
#include "wireproto.H"
#include "wireproto.tmpl"

wireproto_simple_wrapper_type(memlog_idx, unsigned long, val)

wireproto_wrapper_type(memlog_entry)
namespace wireproto {
    template tx_message &tx_message::addparam(
	parameter<list<memlog_entry> >, const list<memlog_entry> &);
    template maybe<error> rx_message::fetch(
	parameter<list<memlog_entry> >,
	list<memlog_entry> &) const;
    template <> maybe<memlog_entry> deserialise(
	wireproto::bufslice &slice)
    {
	auto m(rx_compoundparameter::fetch(slice));
	if (m.isfailure()) return Nothing;
	auto res(memlog_entry::from_compound(*m.success()));
	delete m.success();
	return res;
    }
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
memlog_entry::from_compound(const wireproto::rx_compoundparameter &p)
{
    auto msg_(p.getparam(proto::memlog_entry::msg));
    auto idx(p.getparam(proto::memlog_entry::idx));
    if (msg_ == Nothing || idx == Nothing)
	return Nothing;
    return memlog_entry(idx.just(), msg_.just());
}
maybe<memlog_entry>
memlog_entry::getparam(wireproto::parameter<memlog_entry> tmpl,
		       const wireproto::rx_message &msg)
{
    wireproto::rx_compoundparameter c;
    auto r(msg.fetch(
	       wireproto::parameter<wireproto::rx_compoundparameter>(tmpl),
	       c));
    if (r.isjust()) return Nothing;
    else return from_compound(c);
}
