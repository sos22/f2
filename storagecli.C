#include <err.h>
#include <stdio.h>
#include <unistd.h>

#include "buffer.H"
#include "bytecount.H"
#include "clientio.H"
#include "connpool.H"
#include "fields.H"
#include "job.H"
#include "jobname.H"
#include "logging.H"
#include "parsers.H"
#include "peername.H"
#include "pubsub.H"
#include "storage.H"
#include "streamname.H"
#include "streamstatus.H"

#include "connpool.tmpl"
#include "list.tmpl"
#include "maybe.tmpl"
#include "parsers.tmpl"

class storageclient {
private: connpool &pool;
private: const agentname sn;
private: timedelta timeout;
public:  explicit storageclient(connpool &_pool,
                                const agentname &_sn,
                                timedelta _timeout)
    : pool(_pool),
      sn(_sn),
      timeout(_timeout) {}
public:  orerror<proto::eq::eventid> createjob(clientio,
                                               const job &);
public:  orerror<void> createstream(clientio,
                                    const jobname &,
                                    const streamname &);
public:  orerror<void> append(clientio,
                              const jobname &,
                              const streamname &,
                              bytecount oldsize,
                              const buffer &buf);
public:  orerror<void> finish(clientio,
                              const jobname &,
                              const streamname &);
public:  orerror<pair<size_t, buffer> > read(clientio,
                                             const jobname &,
                                             const streamname &,
                                             maybe<bytecount>,
                                             maybe<bytecount>);
public:  orerror<proto::storage::listjobsres> listjobs(
    clientio,
    const maybe<jobname> &,
    maybe<unsigned>);
public:  orerror<job> statjob(clientio,
                              const jobname &);
public:  orerror<proto::storage::liststreamsres> liststreams(
    clientio,
    const jobname &,
    const maybe<streamname> &,
    maybe<unsigned>);
public:  orerror<streamstatus> statstream(
    clientio,
    const jobname &,
    const streamname &);
public:  orerror<void> removestream(clientio,
                                    const jobname &,
                                    const streamname &);
public:  orerror<void> removejob(clientio,
                                 const jobname &);
public:  ~storageclient(); };

orerror<proto::eq::eventid>
storageclient::createjob(clientio io,
                         const job &j) {
    return pool.call<proto::eq::eventid>(
        io,
        sn,
        interfacetype::storage,
        timeout.future(),
        [&j] (serialise1 &s, connpool::connlock) {
            proto::storage::tag::createjob.serialise(s);
            j.serialise(s); },
        [] (deserialise1 &ds, connpool::connlock) ->
            orerror<proto::eq::eventid> {
            proto::eq::eventid eid(ds);
            if (ds.isfailure()) return ds.failure();
            else return eid; }); }


orerror<void>
storageclient::createstream(clientio io,
                            const jobname &jn,
                            const streamname &stream) {
    return pool.call(
        io,
        sn,
        interfacetype::storage,
        timestamp::now() + timeout,
        [&jn, &stream] (serialise1 &s, connpool::connlock) {
            proto::storage::tag::createstream.serialise(s);
            jn.serialise(s);
            stream.serialise(s); }); }

orerror<void>
storageclient::append(clientio io,
                      const jobname &jn,
                      const streamname &stream,
                      bytecount oldsize,
                      const buffer &buf) {
    return pool.call(
        io,
        sn,
        interfacetype::storage,
        timestamp::now() + timeout,
        [&buf, &jn, oldsize, &stream] (serialise1 &s, connpool::connlock) {
            proto::storage::tag::append.serialise(s);
            jn.serialise(s);
            stream.serialise(s);
            oldsize.serialise(s);
            buf.serialise(s); }); }

orerror<void>
storageclient::finish(clientio io,
                      const jobname &jn,
                      const streamname &stream) {
    return pool.call(
        io,
        sn,
        interfacetype::storage,
        timestamp::now() + timeout,
        [&jn, &stream] (serialise1 &s, connpool::connlock) {
            proto::storage::tag::finish.serialise(s);
            jn.serialise(s);
            stream.serialise(s); }); }

orerror<pair<size_t, buffer> >
storageclient::read(clientio io,
                    const jobname &jn,
                    const streamname &stream,
                    maybe<bytecount> start,
                    maybe<bytecount> end) {
    return pool.call<pair<size_t, buffer> >(
        io,
        sn,
        interfacetype::storage,
        timestamp::now() + timeout,
        [end, &jn, &stream, start] (serialise1 &s, connpool::connlock) {
            proto::storage::tag::read.serialise(s);
            jn.serialise(s);
            stream.serialise(s);
            start.serialise(s);
            end.serialise(s); },
        [] (deserialise1 &ds, connpool::connlock)
            -> orerror<pair<size_t, buffer> >{
            size_t s(ds);
            buffer b(ds);
            if (ds.isfailure()) return ds.failure();
            else return mkpair(s, b); }); }

orerror<proto::storage::listjobsres>
storageclient::listjobs(clientio io,
                        const maybe<jobname> &start,
                        maybe<unsigned> limit) {
    return pool.call<proto::storage::listjobsres>(
        io,
        sn,
        interfacetype::storage,
        timestamp::now() + timeout,
        [limit, &start] (serialise1 &s, connpool::connlock) {
            proto::storage::tag::listjobs.serialise(s);
            start.serialise(s);
            limit.serialise(s); },
        [] (deserialise1 &ds, connpool::connlock)
            -> orerror<proto::storage::listjobsres>{
            proto::storage::listjobsres res(ds);
            if (ds.isfailure()) return ds.failure();
            else return res; }); }

orerror<job>
storageclient::statjob(clientio io,
                       const jobname &jn) {
    return pool.call<job>(
        io,
        sn,
        interfacetype::storage,
        timeout.future(),
        [&jn] (serialise1 &s, connpool::connlock) {
            s.push(proto::storage::tag::statjob);
            s.push(jn); },
        [] (deserialise1 &ds, connpool::connlock) -> orerror<job>{
            job res(ds);
            if (ds.isfailure()) return ds.failure();
            else return res; }); }

orerror<proto::storage::liststreamsres>
storageclient::liststreams(clientio io,
                           const jobname &job,
                           const maybe<streamname> &start,
                           maybe<unsigned> limit) {
    return pool.call<proto::storage::liststreamsres>(
        io,
        sn,
        interfacetype::storage,
        timestamp::now() + timeout,
        [&job, limit, &start] (serialise1 &s, connpool::connlock) {
            proto::storage::tag::liststreams.serialise(s);
            job.serialise(s);
            start.serialise(s);
            limit.serialise(s); },
        [] (deserialise1 &ds, connpool::connlock)
            -> orerror<proto::storage::liststreamsres>{
            proto::storage::liststreamsres res(ds);
            if (ds.isfailure()) return ds.failure();
            else return res; }); }

orerror<streamstatus>
storageclient::statstream(clientio io,
                          const jobname &jn,
                          const streamname &stream) {
    return pool.call<streamstatus>(
        io,
        sn,
        interfacetype::storage,
        timeout.future(),
        [&jn, &stream] (serialise1 &s, connpool::connlock) {
            s.push(proto::storage::tag::statstream);
            s.push(jn);
            s.push(stream); },
        [] (deserialise1 &ds, connpool::connlock) {
            return streamstatus(ds); }); }

orerror<void>
storageclient::removestream(clientio io,
                            const jobname &jn,
                            const streamname &stream) {
    return pool.call(
        io,
        sn,
        interfacetype::storage,
        timestamp::now() + timeout,
        [&jn, &stream] (serialise1 &s, connpool::connlock) {
            proto::storage::tag::removestream.serialise(s);
            jn.serialise(s);
            stream.serialise(s); }); }

orerror<void>
storageclient::removejob(clientio io,
                         const jobname &jn) {
    return pool.call(
        io,
        sn,
        interfacetype::storage,
        timeout.future(),
        [&jn] (serialise1 &s, connpool::connlock) {
            proto::storage::tag::removejob.serialise(s);
            jn.serialise(s); }); }

storageclient::~storageclient() { }

int
main(int argc, char *argv[]) {
    initlogging("storageclient");
    initpubsub();
    if (argc < 4) {
        errx(1, "need at least three arguments: a cluster, a peer and a mode");}
    auto cluster(parsers::__clustername()
                 .match(argv[1])
                 .fatal("parsing cluser name " + fields::mk(argv[1])));
    auto peer(parsers::_agentname()
              .match(argv[2])
              .fatal("parsing agent name " + fields::mk(argv[2])));
    auto pool(connpool::build(cluster).fatal("building conn pool"));
    storageclient conn(*pool, peer, timedelta::seconds(10));
    if (!strcmp(argv[3], "STALL")) {
        if (argc != 4) errx(1, "STALL takes no additional arguments");
        /* Just hang around for a bit so that we can see if PING is
         * working. */
        sleep(10); }
    else if (!strcmp(argv[3], "CREATEJOB")) {
        if (argc != 5) {
            errx(1, "CREATEJOB needs a job name"); }
        auto job(job::parser()
                 .match(argv[4])
                 .fatal("parsing job " + fields::mk(argv[4])));
        auto m = conn.createjob(clientio::CLIENTIO, job);
        if (m == error::already) m.failure().warn("already created");
        else if (m.isfailure()) m.failure().fatal("creating empty job");
        else fields::print("result: " + fields::mk(m.success()) + "\n"); }
    else if (!strcmp(argv[3], "CREATESTREAM")) {
        if (argc != 6) {
            errx(1, "CREATESTREAM needs a job and stream name"); }
        auto job(jobname::parser()
                 .match(argv[4])
                 .fatal("parsing job name " + fields::mk(argv[3])));
        auto stream(streamname::parser()
                    .match(argv[5])
                    .fatal("parsing stream name " + fields::mk(argv[4])));
        auto m = conn.createstream(clientio::CLIENTIO, job, stream);
        if (m == error::already) m.failure().warn("already created");
        else if (m.isfailure()) m.failure().fatal("creating empty stream"); }
    else if (!strcmp(argv[3], "APPEND")) {
        if (argc != 8) {
            errx(1,
                 "APPEND needs a job, a stream name, the old size, "
                 "and a thing to append"); }
        auto job(jobname::parser()
                 .match(argv[4])
                 .fatal("parsing job name " + fields::mk(argv[4])));
        auto stream(streamname::parser()
                    .match(argv[5])
                    .fatal("parsing stream name " + fields::mk(argv[5])));
        auto oldsize(parsers::_bytecount()
                     .match(argv[6])
                     .fatal("parsing byte count " + fields::mk(argv[6])));
        buffer buf;
        buf.queue(argv[7], strlen(argv[7]));
        conn.append(clientio::CLIENTIO, job, stream, oldsize, buf)
            .fatal(fields::mk("appending to stream")); }
    else if (!strcmp(argv[3], "FINISH")) {
        if (argc != 6) {
            errx(1, "FINISH needs a job and a stream name"); }
        auto job(jobname::parser()
                 .match(argv[4])
                 .fatal("parsing job name " + fields::mk(argv[4])));
        auto stream(streamname::parser()
                    .match(argv[5])
                    .fatal("parsing stream name " + fields::mk(argv[5])));
        conn.finish(clientio::CLIENTIO, job, stream)
            .fatal(fields::mk("finishing stream")); }
    else if (!strcmp(argv[3], "READ")) {
        if (argc < 6 || argc > 8) {
            errx(1,
                 "READ needs a job and a stream name, and "
                 "optionally takes a start end end offset"); }
        auto job(jobname::parser()
                 .match(argv[4])
                 .fatal("parsing job name " + fields::mk(argv[4])));
        auto stream(streamname::parser()
                    .match(argv[5])
                    .fatal("parsing stream name " + fields::mk(argv[5])));
        maybe<bytecount> start(Nothing);
        if (argc > 6) {
            start = parsers::_bytecount()
                .match(argv[6])
                .fatal("parsing start offset " + fields::mk(argv[6])); }
        maybe<bytecount> end(Nothing);
        if (argc > 7) {
            end = parsers::_bytecount()
                .match(argv[7])
                .fatal("parsing end offset " + fields::mk(argv[7])); }
        auto r(conn.read(clientio::CLIENTIO, job, stream, start, end)
               .fatal("reading stream"));
        fields::print("size " + fields::mk(r.first()) + "\n");;
        fields::print("content " +
                      fields::mk(r.second()).showshape().words() +
                      "\n"); }
    else if (strcmp(argv[3], "LISTJOBS") == 0) {
        if (argc > 6) {
            errx(1,"LISTJOBS takes optional cursor and limit arguments only"); }
        maybe<jobname> start(Nothing);
        if (argc > 4) {
            start = jobname::parser().match(argv[4])
                .fatal("parsing job name " + fields::mk(argv[4])); }
        maybe<unsigned> limit(Nothing);
        if (argc > 5) {
            limit = parsers::intparser<unsigned>().match(argv[5])
                .fatal("parsing limit " + fields::mk(argv[5])); }
        auto r(conn.listjobs(clientio::CLIENTIO, start, limit)
               .fatal("listing jobs"));
        fields::print(fields::mk(r) + "\n"); }
    else if (strcmp(argv[3], "STATJOB") == 0) {
        if (argc != 5) errx(1,"STATJOB takes jobname argument only");
        jobname j(jobname::parser()
                  .match(argv[4])
                  .fatal("parsing jobname " + fields::mk(argv[4])));
        fields::print(fields::mk(conn.statjob(clientio::CLIENTIO, j)
                                 .fatal("statting job " + fields::mk(j))) +
                      "\n"); }
    else if (strcmp(argv[3], "LISTSTREAMS") == 0) {
        if (argc < 5 || argc > 7) {
            errx(1,
                 "LISTSTREAMS takes a job name and "
                 "optional cursor and limit arguments"); }
        auto job(jobname::parser()
                 .match(argv[4])
                 .fatal("parsing job name " + fields::mk(argv[4])));
        maybe<streamname> start(Nothing);
        if (argc > 5) {
            start = streamname::parser().match(argv[5])
                .fatal("parsing stream name " +fields::mk(argv[5])); }
        maybe<unsigned> limit(Nothing);
        if (argc > 6) {
            limit = parsers::intparser<unsigned>().match(argv[6])
                .fatal("parsing limit " + fields::mk(argv[6])); }
        auto r(conn.liststreams(clientio::CLIENTIO, job, start, limit)
               .fatal("listing streams"));
        fields::print(fields::mk(r) + "\n"); }
    else if (strcmp(argv[3], "STATSTREAM") == 0) {
        if (argc != 6) {
            errx(1, "STATSTREAM takes a job name and a stream name"); }
        auto job(jobname::parser()
                 .match(argv[4])
                 .fatal("parsing job name " + fields::mk(argv[4])));
        auto stream(streamname::parser()
                    .match(argv[5])
                    .fatal("parsing stream name " + fields::mk(argv[5])));
        fields::print(
            "result: " +
            fields::mk(conn.statstream(clientio::CLIENTIO, job, stream)
                       .fatal(fields::mk("statting stream"))) +
            "\n"); }
    else if (strcmp(argv[3], "REMOVESTREAM") == 0) {
        auto job(jobname::parser()
                 .match(argv[4])
                 .fatal("parsing job name " + fields::mk(argv[4])));
        auto stream(streamname::parser()
                    .match(argv[5])
                    .fatal("parsing stream name " + fields::mk(argv[5])));
        conn.removestream(clientio::CLIENTIO, job, stream)
            .fatal(fields::mk("removing stream")); }
    else if (strcmp(argv[3], "REMOVEJOB") == 0) {
        auto job(jobname::parser()
                 .match(argv[4])
                 .fatal("parsing job name " + fields::mk(argv[4])));
        conn.removejob(clientio::CLIENTIO, job)
            .fatal(fields::mk("removing job")); }
    else errx(1, "unknown mode %s", argv[3]);

    pool->destroy();
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging(); }