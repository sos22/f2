#ifndef PROTO_H__
#define PROTO_H__

#include "wireproto.H"

class beaconstatus;
class buffer;
class bufferstatus;
class coordinatorstatus;
class coordinatorconnstatus;
class digest;
class fd_tstatus;
class frequency;
class jobname;
class masternonce;
class memlog_idx;
class memlog_entry;
class nonce;
class peername;
class rpcconnstatus;
class shutdowncode;
class slavename;
class storageslavestatus;
class streamname;
class streamstatus;
struct timeval;
class ratelimiter_status;
class registrationsecret;

namespace proto {
    using namespace wireproto;

    namespace beaconstatus {
        static const parameter<registrationsecret> secret(1);
        static const parameter<class ratelimiter_status> limiter(2);
        static const parameter<unsigned> errors(3);
        static const parameter<unsigned> rx(4);
    };
    namespace bufferstatus {
        static const parameter<unsigned long> prod(1);
        static const parameter<unsigned long> cons(2);
        static const parameter<unsigned int> nrfrags(3);
    };
    namespace coordinatorconnstatus {
        static const parameter< ::rpcconnstatus> conn(1);
        static const parameter<slavename> slave(2);
    };
    namespace coordinatorstatus {
        static const parameter<list< ::coordinatorconnstatus> > conns(1);
    };
    namespace fd_tstatus {
        static const parameter<int> fd(1);
        static const parameter<int> domain(2);
        static const parameter<int> protocol(3);
        static const parameter<int> sndbuf(4);
        static const parameter<int> rcvbuf(5);
        static const parameter<int> sndqueue(6);
        static const parameter<int> rcvqueue(7);
        static const parameter<int> revents(8);
        static const parameter<int> flags(9);
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
    namespace storageslavestatus {
        static const parameter<class ::rpcconnstatus> masterconn(1);
        static const parameter<list< ::rpcconnstatus> > clientconns(2);
    };
    namespace streamstatus {
        static const parameter<class ::streamname> name(1);
        static const parameter<bool> finished(2);
        static const parameter<uint64_t> size(3);
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
            static const parameter< ::storageslavestatus> storageslave(452);
            static const parameter< ::slavename> myname(453);
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
            static const parameter<class peername> peername(802);
            static const parameter<class digest> digest(803);
            static const parameter<class slavename> slavename(804);
        }
    }

    namespace HELLOSLAVE {
        /* Slave hello protocol, used when connecting directly to a
         * slave.  It's assumed that whoever's doing the connecting
         * has already checked that they're connecting to the right
         * place; the aim is to allow the connectee to authenticate
         * the connector.  When someone connects to a slave, the slave
         * immediately sends a fresh nonce (message A).  The connector
         * then replies with message B containing a digest of the
         * nonce plus the registration secret plus the fixed string
         * 'C'. The connectee then either drops the connection or
         * responds with message C. */
        namespace A {
            static const msgtag tag(9);
            static const parameter<nonce> nonce(900);
        }
        namespace B {
            static const msgtag tag(10);
            /* Digest of 'C'+nonce+registration secret */
            static const parameter<class digest> digest(950);
        }
        namespace C {
            static const msgtag tag(11);
            /* No parameters */
        }
    }

    /* Storage slave: Create a new empty output stream.  Existing
       empty and partial streams are silently replaced.  Requests to
       create which already exists and which is already full return
       error::already. */
    namespace CREATEEMPTY {
        static const msgtag tag(12);
        namespace req {
            static const parameter< ::jobname> job(1200);
            static const parameter< ::streamname> stream(1201);
        }
        /* No response parameters, except for possible error */
    }
    /* Storage slave: add some bytes to the end of an output stream.
       The stream must already exist. */
    namespace APPEND {
        static const msgtag tag(13);
        namespace req {
            static const parameter< ::jobname> job(1300);
            static const parameter< ::streamname> stream(1301);
            static const parameter< ::buffer> bytes(1302);
        }
        /* No response parameters, except for possible error */
    }
    /* Storage slave: mark a stream as completed. */
    namespace FINISH {
        static const msgtag tag(14);
        namespace req {
            static const parameter< ::jobname> job(1400);
            static const parameter< ::streamname> stream(1401);
        }
        /* No response parameters, except for possible error */
    }
    /* Storage slave: read a chunk of a stream.  The stream must be
     * complete. */
    namespace READ {
        static const msgtag tag(15);
        namespace req {
            static const parameter< ::jobname> job(1400);
            static const parameter< ::streamname> stream(1401);
            /* Optional limits on the data to read back.  start is
               inclusive and end exclusive.  start defaults to zero
               and end defaults to the size of the stream.  Values
               beyond the end of the stream are silently truncated to
               the stream size.  It is permissible to specify start
               equal to end, and in that case an empty buffer is
               returned.  */
            static const parameter<unsigned long> start(1402);
            static const parameter<unsigned long> end(1403);
        }
        namespace resp {
            /* The read returns a buffer of data which always starts
               at the start parameter (or 0, if the start parameter is
               missing) but which might be truncated to finish before
               the specified end.  Requests beyond the end of the
               stream are always truncated; other requests may be
               truncated at the discretion of the thing generating the
               response.  The response buffer will always contain at
               least one byte if any part of the request range falls
               within the defined range of the stream. */
            static const parameter< ::buffer> bytes(1450);
            /* Total size of the stream, in bytes.  Present and
               correct even when buffer is truncated. */
            static const parameter<unsigned long> size(1451);
        }
    }
    /* Storage slave: list all of the jobs which have any streams on
       this slave. */
    namespace LISTJOBS {
        static const msgtag tag(16);
        namespace req {
            /* Optional cursor from which to start returning results.
             * If not present, start from the beginning. */
            static const parameter< ::jobname> cursor(1600);
            /* Optional limit on number of results to return.  If not
             * present, return all of them. */
            static const parameter< unsigned> limit(1601);
        }
        namespace resp {
            /* Cursor from which client can restart iteration to get
             * the rest of the results, or Nothing if there are no
             * more. */
            static const parameter< ::jobname> cursor(1650);
            static const parameter<list< ::jobname> > jobs(1651);
        }
    }
    /* Storage slave: list all of the streams which a given job has on
     * this state, and some information about their state. */
    namespace LISTSTREAMS {
        static const msgtag tag(17);
        namespace req {
            static const parameter< ::jobname> job(1700);
            /* Optional cursor from which to start returning results.
             * If not present, start from the beginning. */
            static const parameter< ::streamname> cursor(1701);
            /* Optional limit on number of results to return.  If not
             * present, return all of them. */
            static const parameter< unsigned> limit(1702);
        }
        namespace resp {
            /* Cursor from which client can restart iteration to get
             * the rest of the results, or Nothing if there are no
             * more. */
            static const parameter< ::streamname> cursor(1750);
            static const parameter<list< ::streamstatus> > streams(1751);
        }
    }
    /* Storage slave: remove a stream from the slave.  Jobs are removed
     * automatically when the last stream is removed. */
    namespace REMOVESTREAM {
        static const msgtag tag(18);
        namespace req {
            static const parameter< ::jobname> job(1800);
            static const parameter< ::streamname> stream(1801);
        }
        /* No response parameters, except possibly an error
         * indication. */
    }
}

#endif /* !PROTO_H__ */
