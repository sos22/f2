#include <stdio.h>
#include <string.h>

#include "controlclient.H"
#include "proto.H"
#include "wireproto.H"

int
main(int argc, char *argv[])
{
    auto c(controlclient::connect());
    int r;

    if (c.isfailure())
	c.failure().fatal("connecting to master");

    r = 0;
    if (!strcmp(argv[1], "PING")) {
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
    } else if (!strcmp(argv[1], "LOGS")) {
	auto snr(c.success()->allocsequencenr());
	auto m = c.success()->call(
	    wireproto::req_message(proto::GETLOGS::tag, snr));
	c.success()->putsequencenr(snr);
	if (m.issuccess()) {
	    list<const char *> msgs;
	    auto r(m.success()->fetch(proto::GETLOGS::resp::msgs, msgs));
	    if (r.isjust())
		r.just().fatal("decoding returned message list");
	    for (auto it(msgs.start()); !it.finished(); it.next())
		printf("%s\n", *it);
	    msgs.flush();
	} else {
	    m.failure().fatal("requesting logs");
	}
    } else {
	printf("Unknown command %s.  Known: PING, LOGS\n", argv[1]);
	r = 1;
    }
    c.success()->destroy();
    return r;
}
