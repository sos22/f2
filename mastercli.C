#include <stdio.h>

#include "controlclient.H"
#include "proto.H"
#include "wireproto.H"

int
main()
{
    auto c(controlclient::connect());
    if (c.isfailure())
	c.failure().fatal("connecting to master");
    auto snr(c.success()->allocsequencenr());
    auto m = c.success()->call(
	wireproto::req_message(proto::PING::tag, snr).
	addparam(proto::PING::req::msg, "Hello"));
    c.success()->putsequencenr(snr);
    if (m.issuccess())
	printf("master ping sequence %d, message %s\n",
	       m.success()->getparam(proto::PING::resp::cntr).just(),
	       m.success()->getparam(proto::PING::resp::msg).just());
    else
	m.failure().fatal("sending ping");
    c.success()->destroy();
    return 0;
}
