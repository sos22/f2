#include <stdio.h>
#include <string.h>

#include "controlclient.H"
#include "logging.H"
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
	memlog_idx cursor(memlog_idx::min);
	while (1) {
	    auto snr(c.success()->allocsequencenr());
	    auto m = c.success()->call(
		wireproto::req_message(proto::GETLOGS::tag, snr)
		.addparam(proto::GETLOGS::req::startidx, cursor));
	    c.success()->putsequencenr(snr);
	    if (m.isfailure())
		m.failure().fatal("requesting logs");
	    auto mm(m.success());
	    list<memlog_entry> msgs;
	    auto r(mm->fetch(proto::GETLOGS::resp::msgs, msgs));
	    if (r.isjust())
		r.just().fatal("decoding returned message list");
	    for (auto it(msgs.start()); !it.finished(); it.next())
		printf("%9ld: %s\n", it->idx.as_long(), it->msg);
	    msgs.flush();
	    auto s(mm->getparam(proto::GETLOGS::resp::resume));
	    mm->finish();
	    if (!s)
		break; /* we're done */
	    cursor = s.just();
	}
    } else {
	printf("Unknown command %s.  Known: PING, LOGS\n", argv[1]);
	r = 1;
    }
    c.success()->destroy();
    return r;
}
