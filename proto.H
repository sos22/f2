#ifndef PROTO_H__
#define PROTO_H__

#include "wireproto.H"

namespace proto {
    using namespace wireproto;

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

}

#endif /* !PROTO_H__ */
