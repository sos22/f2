/* XXX A bunch of static const constants in a header file isn't a
 * great sign. */
#ifndef PROTO_H__
#define PROTO_H__

#include "wireproto.H"

class actortype;
class beaconclientstatus;
class beaconserverstatus;
class buffer;
class bufferstatus;
class buildconfig;
class clustername;
class connpoolstatus;
class digest;
class fd_tstatus;
class frequency;
class jobname;
class listenfdstatus;
class masternonce;
class memlog_idx;
class memlog_entry;
class nonce;
class peername;
class peernameport;
class rpcconnconfig;
class rpcconnstatus;
class rpcserverstatus;
class rpcservicestatus;
class shutdowncode;
class slavename;
class streamname;
class streamstatus;
class ratelimiterconfig;
class ratelimiter_status;
class registrationsecret;
class timedelta;
class version;
class walltime;

namespace proto {
    using namespace wireproto;

    namespace bufferstatus {
        static const parameter<unsigned long> prod(1);
        static const parameter<unsigned long> cons(2);
        static const parameter<unsigned int> nrfrags(3);
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
    namespace listenfdstatus {
        static const parameter< ::peername> listenon(1);
        static const parameter<int> fd(2);
        static const parameter<int> domain(3);
        static const parameter<int> protocol(4);
        static const parameter<int> flags(5);
        static const parameter<int> revents(6);
    };
    namespace memlog_entry {
        static const parameter<const char *> msg(1);
        static const parameter<memlog_idx> idx(2);
    };
    namespace peername {
        static const parameter<const char *> local(1);
        static const parameter<peernameport> port(2);
        static const parameter<uint32_t> ipv4(3);
        static const parameter<uint64_t> ipv6a(4);
        static const parameter<uint64_t> ipv6b(5);
        static const parameter<bool> wildcardlocal(6);
    };
    namespace streamstatus {
        static const parameter<class ::streamname> name(1);
        static const parameter<bool> finished(2);
        static const parameter<uint64_t> size(3);
    };

    /* Every server implements the no-op PING interface, which is
     * mostly just there for testing. */
    namespace PING {
        static const msgtag tag(1);
        /* No request parameters */
        /* No response parameters */
    };
    /* Control interface */

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
        /* No response parameters */
    };

    namespace STATUS {
        static const msgtag tag(4);
        /* no request parameters */
        /* no response parameters: it all goes in the logs */
    };

    namespace BUILDCONFIG {
        static const msgtag tag(5);
        /* no request parameters */
        namespace resp {
            static const parameter< ::buildconfig> config(550);
        };
    };

    /* Find all of the ports we're listening on.  The same information
       is also exposed throughthe STATUS interface, but that interface
       is intended for debugging, so should be easy to change and
       shouldn't be used for ``real'' work, whereas this one is
       intended to be consumed by other programs, and will therefore
       remain stable. */
    namespace LISTENING {
        static const msgtag tag(6);
        /* no request parameters */
        namespace resp {
            static const parameter< ::peername> control(651);
            static const parameter< ::peername> coordinator(652);
            static const parameter< ::peername> storageslave(653);
        }
    }

    /* Beacon interface.  Every server in the system is expected to
     * send periodic BEACON::resp messages to a broadcast (or in
     * future multicast) interface advertising their capabilities and
     * the address on which peers should connect to them.  Anything
     * which can act as a client should listen for these messages and
     * use them to build up a (slavename,actortype)->peername mapping.
     * Servers should also listen for BEACON::req messages and respond
     * to them with unicast BEACON::resp messages to whoever sent the
     * req; these are used to populate an initial start-of-day table.
     */
    namespace BEACON {
        namespace req {
            static const msgtag tag(700);
            /* Protocol version */
            static const parameter< ::version> version(800);
            /* Filter on who should respond: */
            /* Always filter by cluster */
            static const parameter<clustername> cluster(801);
            /* Optionally filter by slavename */
            static const parameter<maybe<slavename> > name(802);
            /* Optionally filter by type */
            static const parameter<maybe<actortype> > type(803);
        }
        namespace resp {
            static const msgtag tag(750);
            /* Protocol version */
            static const parameter< ::version> version(750);
            /* What interface do we expose? */
            static const parameter<actortype> type(751);
            /* Persistent identifier for this slave */
            static const parameter<slavename> name(752);
            /* Which cluster are we part of? */
            static const parameter<clustername> cluster(753);
            /* What TCP port do we offer service on?  The host part of
             * the peername should be taken from the response's source
             * address. */
            static const parameter<peernameport> port(754);
            /* How long may the recipient beacon client cache this
             * result for? */
            static const parameter<timedelta> cachetime(755);
        }
    }

    /* The first request made by a client when it connects to a server
     * must always be HELLO */
    namespace HELLO {
        static const msgtag tag(8);
        namespace req {
            static const parameter< ::version> version(801);
        }
        /* No response parameters (except possibly for an error) */
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
            static const parameter<maybe< ::jobname> > cursor(1600);
            /* Optional limit on number of results to return.  If not
             * present, return all of them. */
            static const parameter<maybe<unsigned> > limit(1601);
        }
        namespace resp {
            /* Cursor from which client can restart iteration to get
             * the rest of the results, or Nothing if there are no
             * more. */
            static const parameter<maybe< ::jobname> > cursor(1650);
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
            static const parameter<maybe< ::streamname > > cursor(1701);
            /* Optional limit on number of results to return.  If not
             * present, return all of them. */
            static const parameter<maybe<unsigned> > limit(1702);
        }
        namespace resp {
            /* Cursor from which client can restart iteration to get
             * the rest of the results, or Nothing if there are no
             * more. */
            static const parameter<maybe< ::streamname> > cursor(1750);
            static const parameter<list< ::streamstatus> > streams(1751);
        }
    }
    /* Storage slave: remove a stream from the slave.  Jobs are
     * removed automatically when the last stream is removed.  Returns
     * error::already if the stream does not exist. */
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
