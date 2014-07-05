#ifndef PROTO_H__
#define PROTO_H__

#include "wireproto.H"

class beaconstatus;
class bufferstatus;
class coordinatorstatus;
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

    namespace beaconstatus {
        static const parameter<registrationsecret> secret(1);
        static const parameter<class ratelimiter_status> limiter(2);
        static const parameter<unsigned> errors(3);
        static const parameter<unsigned> rx(3);
    };
    namespace bufferstatus {
        static const parameter<unsigned long> prod(1);
        static const parameter<unsigned long> cons(2);
        static const parameter<unsigned int> nrfrags(3);
    };
    namespace coordinatorstatus {
        static const parameter<list< ::rpcconnstatus> > conns(1);
    };
    namespace fd_tstatus {
        static const parameter<int> fd(1);
        static const parameter<timeval> lastrx(2);
        static const parameter<int> revents(3);
    };
    namespace memlog_entry {
        static const parameter<const char *> msg(1);
        static const parameter<memlog_idx> idx(2);
    };
    namespace peername {
        static const parameter<const char *> local(1);
        static const parameter<uint16_t> port(2);
        static const parameter<uint32_t> ipv4(3);
        static const parameter<uint64_t> ipv6a(3);
        static const parameter<uint64_t> ipv6b(3);
    };
    namespace ratelimiter_status {
        static const parameter<frequency> max_rate(1);
        static const parameter<unsigned> bucket_size(2);
        static const parameter<unsigned> bucket_content(3);
        static const parameter<unsigned> dropped(4);
    };
    namespace rpcconnstatus {
        static const parameter<class ::bufferstatus> outgoing(1);
        static const parameter<class ::fd_tstatus> fd(2);
        static const parameter<wireproto::sequencerstatus> sequencer(3);
        static const parameter<list<wireproto::rx_messagestatus> >
            pendingrx(4);
        static const parameter<class ::peername> peername(5);
        static const parameter<struct ::timeval> lastcontact(6);
    };
    namespace timeval {
        static const parameter<long> tv_sec(1);
        static const parameter<long> tv_usec(1);
    };

    /* Control interface */
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
            /* optional */
            static const parameter<unsigned> nr(201);
        };
        namespace resp {
            static const parameter<memlog_idx> resume(250);
            static const parameter<list< ::memlog_entry> > msgs(251);
        };
    };

    namespace QUIT {
        static const msgtag tag(3);
        namespace req {
            static const parameter<const char *> message(300);
            static const parameter<shutdowncode> reason(301);
        };
        /* No response */
    };

    namespace STATUS {
        static const msgtag tag(4);
        /* no request parameters */
        namespace resp {
            static const parameter< ::beaconstatus> beacon(450);
            static const parameter< ::coordinatorstatus> coordinator(451);
        };
    };

    /* Beacon interface */
    namespace HAIL {
        static const msgtag tag(6);
        namespace req {
            static const parameter<unsigned> version(600);
            static const parameter<class nonce> nonce(601);
        }
        namespace resp {
            static const parameter<unsigned> version(650);
            static const parameter<class peername> mastername(651);
            static const parameter<class peername> slavename(652);
            static const parameter<class masternonce> nonce(653);
            static const parameter<class digest> digest(654);
        }
    }

    namespace HELLO {
        static const msgtag tag(8);
        namespace req {
            static const parameter<unsigned> version(800);
            static const parameter<class masternonce> nonce(801);
            static const parameter<class peername> slavename(802);
            static const parameter<class digest> digest(803);
        }
    }
}

#endif /* !PROTO_H__ */
