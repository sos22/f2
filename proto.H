#ifndef PROTO_H__
#define PROTO_H__

#include "wireproto.H"

namespace proto {
    using namespace wireproto;

    namespace PING {
	static const msgtag tag(1);
	static const parameter<const char *> msg(1);
    };

    namespace PONG {
	static const msgtag tag(2);
    };
}

#endif /* !PROTO_H__ */
