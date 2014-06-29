#ifndef PROTO_H__
#define PROTO_H__

#include "wireproto.H"

class coordinatorconnstatus;
class digest;
class fd_tstatus;
class frequency;
class masternonce;
class memlog_idx;
class memlog_entry;
class nonce;
class peername;
class rpcconnstatus;
class shutdowncode;
struct timeval;
class ratelimiter_status;
class registrationsecret;

namespace proto {
    using namespace wireproto;

    namespace coordinatorconnstatus {
        static const parameter<int> active(1, "active");
        static const parameter<struct timeval> lastcontact(2, "lastcontact");
        static const parameter<rpcconnstatus> conn(3, "conn");
    };
    namespace fd_tstatus {
        static const parameter<int> fd(1, "fd");
        static const parameter<timeval> lastrx(2, "lastrx");
        static const parameter<int> revents(3, "revents");
    };
    namespace memlog_entry {
        static const parameter<const char *> msg(1, "msg");
        static const parameter<memlog_idx> idx(2, "idx");
    };
    namespace peername {
        static const parameter<const char *> local(1, "local");
        static const parameter<uint16_t> port(2, "port");
        static const parameter<uint32_t> ipv4(3, "ipv4");
        static const parameter<uint64_t> ipv6a(3, "ipv6a");
        static const parameter<uint64_t> ipv6b(3, "ipv6b");
    };
    namespace ratelimiter_status {
        static const parameter<frequency> max_rate(1, "max_rate");
        static const parameter<unsigned> bucket_size(2, "bucket_size");
        static const parameter<unsigned> bucket_content(3, "bucket_content");
        static const parameter<unsigned> dropped(4, "dropped");
    };
    namespace rpcconnstatus {
        static const parameter<class ::fd_tstatus> fd(1, "fd");
        static const parameter<class ::peername> peername(2, "peername");
    };
    namespace timeval {
        static const parameter<long> tv_sec(1, "tv_sec");
        static const parameter<long> tv_usec(1, "tv_usec");
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
    namespace HAIL {
        static const msgtag tag(6);
        namespace req {
            static const parameter<unsigned> version(600, "version");
            static const parameter<class nonce> nonce(601, "nonce");
        }
        namespace resp {
            static const parameter<unsigned> version(650, "version");
            static const parameter<class peername> mastername(651,
                                                              "mastername");
            static const parameter<class peername> slavename(652,
                                                             "slavename");
            static const parameter<class masternonce> nonce(653, "nonce");
            static const parameter<class digest> digest(654, "digest");
        }
    }

    namespace BEACONSLAVESTATUS {
        static const msgtag tag(7);
    };

    namespace HELLO {
        static const msgtag tag(8);
        namespace req {
            static const parameter<unsigned> version(800, "version");
            static const parameter<class masternonce> nonce(801, "nonce");
            static const parameter<class peername> slavename(802, "slavename");
            static const parameter<class digest> digest(803, "digest");
        }
    }

    namespace COORDINATORSTATUS {
        static const msgtag tag(9);
        /* No request parameters */
        namespace resp {
            static const parameter<class ratelimiter_status>
                ratelimiter(950, "ratelimiter");
            static const parameter<list< ::coordinatorconnstatus> >
                conns(951, "conns");
        }
    }
}

#endif /* !PROTO_H__ */
