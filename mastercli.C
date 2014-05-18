#include "controlclient.H"
#include "proto.H"
#include "wireproto.H"

int
main()
{
    auto c(controlclient::connect());
    if (c.isfailure())
	c.failure().fatal("connecting to master");
    c.success()->send(
	wireproto::tx_message(proto::PING::tag).
	addparam(proto::PING::msg, "Hello"));
    c.success()->destroy();
    return 0;
}
