#ifndef PROTO_H__
#define PROTO_H__

#include "wireproto.H"

class memlog_idx;
class memlog_complete;
class memlog_entry;

namespace proto {
    using namespace wireproto;

    namespace memlog_entry {
	static const parameter<const char *> msg(1);
	static const parameter<memlog_idx> idx(2);
    };

    namespace PING {
	static const msgtag tag(1);
	namespace req {
	    static const parameter<const char *> msg(100);
	};
	namespace resp {
	    static const parameter<int> cntr(150);
	    static const parameter<const char *> msg(151);
	};
    };

    namespace GETLOGS {
	static const msgtag tag(2);
	namespace req {
	    /* optional */
	    static const parameter<memlog_idx> startidx(200);
	};
	namespace resp {
	    static const parameter<memlog_complete> complete(250);
	    static const parameter<list< ::memlog_entry> > msgs(251);
	    static const parameter<error> failure(252);
	};
    };
}

#endif /* !PROTO_H__ */
