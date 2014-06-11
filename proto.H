#ifndef PROTO_H__
#define PROTO_H__

#include "wireproto.H"

class frequency;
class memlog_idx;
class memlog_entry;
class shutdowncode;
class ratelimiter_status;
class registrationsecret;

namespace proto {
    using namespace wireproto;

    namespace memlog_entry {
        static const parameter<const char *> msg(1, "msg");
        static const parameter<memlog_idx> idx(2, "idx");
    };
    namespace ratelimiter_status {
        static const parameter<frequency> max_rate(1, "max_rate");
        static const parameter<unsigned> bucket_size(2, "bucket_size");
        static const parameter<unsigned> bucket_content(3, "bucket_content");
    };

    /* Control interface */
    namespace PING {
        static const msgtag tag(1);
        namespace req {
            static const parameter<const char *> msg(100, "msg");
        };
        namespace resp {
            static const parameter<int> cntr(150, "cntr");
            static const parameter<const char *> msg(151, "msg");
        };
    };

    namespace GETLOGS {
        static const msgtag tag(2);
        namespace req {
            /* optional */
            static const parameter<memlog_idx> startidx(200, "startidx");
        };
        namespace resp {
            static const parameter<memlog_idx> resume(250, "resume");
            static const parameter<list< ::memlog_entry> > msgs(251, "msgs");
        };
    };

    namespace QUIT {
        static const msgtag tag(3);
        namespace req {
            static const parameter<const char *> message(300, "message");
            static const parameter<shutdowncode> reason(301, "reason");
        };
        /* No response */
    };

    namespace BEACONSTATUS {
        static const msgtag tag(4);
        /* no request parameters */
        namespace resp {
            static const parameter<registrationsecret> secret(450, "secret");
            static const parameter<class ratelimiter_status> limiter(451,
                                                                     "limiter");
            static const parameter<unsigned> errors(452, "errors");
            static const parameter<unsigned> rx(453, "rx");
        };
    };

    namespace BEACONCONFIGURE {
        static const msgtag tag(5);
    };

    /* Beacon interface */
    namespace BEACONPROBE {
        static const msgtag tag(6);
    };
}

#endif /* !PROTO_H__ */
