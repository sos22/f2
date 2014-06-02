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
    auto m = c.success()->call(
	wireproto::tx_message(proto::PING::tag, c.success()->allocsequencenr()).
	addparam(proto::PING::msg, "Hello"));
    if (m.issuccess())
	printf("master ping sequence %d, message %s\n",
	       m.success()->getparam(proto::PONG::cntr).just(),
	       m.success()->getparam(proto::PONG::msg).just());
    else
	m.failure().fatal("sending ping");
    c.success()->destroy();
    return 0;
}
