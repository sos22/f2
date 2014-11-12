#include <err.h>
#include <stdio.h>
#include <unistd.h>

#include "buffer.H"
#include "clientio.H"
#include "fields.H"
#include "jobname.H"
#include "logging.H"
#include "parsers.H"
#include "peername.H"
#include "proto.H"
#include "pubsub.H"
#include "rpcclient2.H"
#include "storage.H"
#include "streamname.H"
#include "streamstatus.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "parsers.tmpl"
#include "rpcclient2.tmpl"
#include "wireproto.tmpl"

#include "fieldfinal.H"

class storageclient {
private: rpcclient2 *const inner;
private: explicit storageclient(rpcclient2 *_inner) : inner(_inner) {}
public:  static orerror<nnp<storageclient> > connect(
    clientio,
    const peername &);
public:  orerror<void> createempty(clientio,
                                   const jobname &,
                                   const streamname &);
public:  orerror<void> append(clientio,
                              const jobname &,
                              const streamname &,
                              const buffer &buf);
public:  orerror<void> finish(clientio,
                              const jobname &,
                              const streamname &);
public:  orerror<pair<size_t, buffer> > read(clientio,
                                             const jobname &,
                                             const streamname &,
                                             maybe<unsigned long>,
                                             maybe<unsigned long>);
public:  orerror<pair<maybe<jobname>, list<jobname> > > listjobs(
    clientio,
    const maybe<jobname> &,
    maybe<unsigned>);
public:  orerror<pair<maybe<streamname>, list<streamstatus> > > liststreams(
    clientio,
    const jobname &,
    const maybe<streamname> &,
    maybe<unsigned>);
public:  orerror<void> removestream(clientio,
                                    const jobname &,
                                    const streamname &);
public:  ~storageclient(); };

orerror<nnp<storageclient> >
storageclient::connect(clientio io, const peername &p) {
    auto conn(rpcclient2::connect(io, p));
    if (conn.isfailure()) return conn.failure();
    else return _nnp(*new storageclient(conn.success())); }

orerror<void>
storageclient::createempty(clientio io,
                           const jobname &jn,
                           const streamname &sn) {
    return inner->call<void>(
        io,
        interfacetype::storage,
        [&jn, &sn] (serialise1 &s, mutex_t::token /* txlock */) {
            proto::storage::tag::createempty.serialise(s);
            jn.serialise(s);
            sn.serialise(s); },
        [] (deserialise1 &, rpcclient2::onconnectionthread) -> orerror<void> {
            return Success; }); }

orerror<void>
storageclient::append(clientio io,
                      const jobname &jn,
                      const streamname &sn,
                      const buffer &buf) {
    return inner->call<void>(
        io,
        interfacetype::storage,
        [&buf, &jn, &sn] (serialise1 &s, mutex_t::token /* txlock */) {
            proto::storage::tag::append.serialise(s);
            jn.serialise(s);
            sn.serialise(s);
            buf.serialise(s); },
        [] (deserialise1 &, rpcclient2::onconnectionthread) -> orerror<void> {
            return Success; }); }

orerror<void>
storageclient::finish(clientio io,
                      const jobname &jn,
                      const streamname &sn) {
    return inner->call<void>(
        io,
        interfacetype::storage,
        [&jn, &sn] (serialise1 &s, mutex_t::token /* txlock */) {
            proto::storage::tag::finish.serialise(s);
            jn.serialise(s);
            sn.serialise(s); },
        [] (deserialise1 &, rpcclient2::onconnectionthread) -> orerror<void> {
            return Success; }); }

orerror<pair<size_t, buffer> >
storageclient::read(clientio io,
                    const jobname &jn,
                    const streamname &sn,
                    maybe<unsigned long> start,
                    maybe<unsigned long> end) {
    return inner->call<pair<size_t, buffer> >(
        io,
        interfacetype::storage,
        [end, &jn, &sn, start] (serialise1 &s, mutex_t::token /* txlock */) {
            proto::storage::tag::read.serialise(s);
            jn.serialise(s);
            sn.serialise(s);
            start.serialise(s);
            end.serialise(s); },
        []
        (deserialise1 &ds, rpcclient2::onconnectionthread)
            -> orerror<pair<size_t, buffer> > {
            size_t s(ds);
            buffer b(ds);
            if (ds.isfailure()) return ds.failure();
            else return mkpair(s, b); }); }

orerror<pair<maybe<jobname>, list<jobname> > >
storageclient::listjobs(clientio io,
                        const maybe<jobname> &start,
                        maybe<unsigned> limit) {
    return inner->call<pair<maybe<jobname>, list<jobname> > >(
        io,
        interfacetype::storage,
        [limit, &start] (serialise1 &s, mutex_t::token /* txlock */) {
            proto::storage::tag::listjobs.serialise(s);
            start.serialise(s);
            limit.serialise(s); },
        [] (deserialise1 &ds, rpcclient2::onconnectionthread)
            -> orerror<pair<maybe<jobname>, list<jobname> > >{
            maybe<jobname> newcursor(ds);
            list<jobname> res(ds);
            if (ds.isfailure()) return ds.failure();
            else return mkpair(newcursor, res); }); }

orerror<pair<maybe<streamname>, list<streamstatus> > >
storageclient::liststreams(clientio io,
                           const jobname &job,
                           const maybe<streamname> &start,
                           maybe<unsigned> limit) {
    return inner->call<pair<maybe<streamname>, list<streamstatus> > >(
        io,
        interfacetype::storage,
        [&job, limit, &start] (serialise1 &s, mutex_t::token /* txlock */) {
            proto::storage::tag::liststreams.serialise(s);
            job.serialise(s);
            start.serialise(s);
            limit.serialise(s); },
        [] (deserialise1 &ds, rpcclient2::onconnectionthread)
            -> orerror<pair<maybe<streamname>, list<streamstatus> > >{
            maybe<streamname> newcursor(ds);
            list<streamstatus> res(ds);
            if (ds.isfailure()) return ds.failure();
            else return mkpair(newcursor, res); }); }

orerror<void>
storageclient::removestream(clientio io,
                            const jobname &jn,
                            const streamname &sn) {
    return inner->call<void>(
        io,
        interfacetype::storage,
        [&jn, &sn] (serialise1 &s, mutex_t::token /* txlock */) {
            proto::storage::tag::removestream.serialise(s);
            jn.serialise(s);
            sn.serialise(s); },
        [] (deserialise1 &, rpcclient2::onconnectionthread) -> orerror<void> {
            return Success; }); }

storageclient::~storageclient() { inner->destroy(); }

int
main(int argc, char *argv[]) {
    initlogging("storageclient");
    initpubsub();
    if (argc < 3) {
        errx(1, "need at least two arguments: a peer and a mode");}
    auto peer(parsers::_peername()
              .match(argv[1])
              .fatal("parsing peername " + fields::mk(argv[1])));
    auto conn(storageclient::connect(
                  clientio::CLIENTIO,
                  peer)
              .fatal("connecting to " + fields::mk(peer)));
    if (!strcmp(argv[2], "STALL")) {
        if (argc != 3) errx(1, "STALL takes no additional arguments");
        /* Just hang around for a bit so that we can see if PING is
         * working. */
        sleep(10); }
    else if (!strcmp(argv[2], "CREATEEMPTY")) {
        if (argc != 5) {
            errx(1, "CREATEEMPTY needs a job and stream name"); }
        auto job(parsers::_jobname()
                 .match(argv[3])
                 .fatal("parsing job name " + fields::mk(argv[3])));
        auto stream(parsers::_streamname()
                    .match(argv[4])
                    .fatal("parsing stream name " + fields::mk(argv[4])));
        auto m = conn->createempty(clientio::CLIENTIO, job, stream);
        if (m == error::already)
            m.failure().warn("already created");
        else if (m.isfailure())
            m.failure().fatal("creating empty file"); }
    else if (!strcmp(argv[2], "APPEND")) {
        if (argc != 6) {
            errx(1,
                 "APPEND needs a job, a stream name, and a thing to append"); }
        auto job(parsers::_jobname()
                 .match(argv[3])
                 .fatal("parsing job name " + fields::mk(argv[4])));
        auto stream(parsers::_streamname()
                    .match(argv[4])
                    .fatal("parsing stream name " + fields::mk(argv[5])));
        buffer buf;
        buf.queue(argv[5], strlen(argv[5]));
        conn->append(clientio::CLIENTIO, job, stream, buf)
            .fatal(fields::mk("appending to stream")); }
    else if (!strcmp(argv[2], "FINISH")) {
        if (argc != 5) {
            errx(1, "FINISH needs a job and a stream name"); }
        auto job(parsers::_jobname()
                 .match(argv[3])
                 .fatal("parsing job name " + fields::mk(argv[3])));
        auto stream(parsers::_streamname()
                    .match(argv[4])
                    .fatal("parsing stream name " + fields::mk(argv[4])));
        conn->finish(clientio::CLIENTIO, job, stream)
            .fatal(fields::mk("finishing stream")); }
    else if (!strcmp(argv[2], "READ")) {
        if (argc < 5 || argc > 7) {
            errx(1,
                 "READ needs a job and a stream name, and "
                 "optionally takes a start end end offset"); }
        auto job(parsers::_jobname()
                 .match(argv[3])
                 .fatal("parsing job name " + fields::mk(argv[3])));
        auto stream(parsers::_streamname()
                    .match(argv[4])
                    .fatal("parsing stream name " + fields::mk(argv[4])));
        maybe<unsigned long> start(Nothing);
        if (argc > 5) {
            start = parsers::intparser<unsigned long>()
                .match(argv[5])
                .fatal("parsing start offset " + fields::mk(argv[5])); }
        maybe<unsigned long> end(Nothing);
        if (argc > 6) {
            end = parsers::intparser<unsigned long>()
                .match(argv[6])
                .fatal("parsing end offset " + fields::mk(argv[6])); }
        auto r(conn->read(clientio::CLIENTIO, job, stream, start, end)
               .fatal("reading stream"));
        fields::print("size " + fields::mk(r.first()) + "\n");;
        fields::print("content " +
                      fields::mk(r.second()).showshape().words() +
                      "\n"); }
    else if (strcmp(argv[2], "LISTJOBS") == 0) {
        if (argc > 5) {
            errx(1,"LISTJOBS takes optional cursor and limit arguments only"); }
        maybe<jobname> start(Nothing);
        if (argc > 3) {
            start = parsers::_jobname().match(argv[3])
                .fatal("parsing job name " + fields::mk(argv[3])); }
        maybe<unsigned> limit(Nothing);
        if (argc > 4) {
            limit = parsers::intparser<unsigned>().match(argv[4])
                .fatal("parsing limit " + fields::mk(argv[4])); }
        auto r(conn->listjobs(clientio::CLIENTIO, start, limit)
               .fatal("listing jobs"));
        fields::print("cursor: " + fields::mk(r.first()) + "\n");
        fields::print("jobs: " + fields::mk(r.second()) + "\n"); }
    else if (strcmp(argv[2], "LISTSTREAMS") == 0) {
        if (argc < 4 || argc > 6) {
            errx(1,
                 "LISTSTREAMS takes a job name and "
                 "optional cursor and limit arguments"); }
        auto job(parsers::_jobname()
                 .match(argv[3])
                 .fatal("parsing job name " + fields::mk(argv[3])));
        maybe<streamname> start(Nothing);
        if (argc > 4) {
            start = parsers::_streamname().match(argv[4])
                .fatal("parsing stream name " +fields::mk(argv[4])); }
        maybe<unsigned> limit(Nothing);
        if (argc > 5) {
            limit = parsers::intparser<unsigned>().match(argv[5])
                .fatal("parsing limit " + fields::mk(argv[5])); }
        auto r(conn->liststreams(clientio::CLIENTIO, job, start, limit)
               .fatal("listing streams"));
        fields::print("cursor: " + fields::mk(r.first()) + "\n");
        fields::print("streams: " + fields::mk(r.second()) + "\n"); }
    else if (strcmp(argv[2], "REMOVESTREAM") == 0) {
        auto job(parsers::_jobname()
                 .match(argv[3])
                 .fatal("parsing job name " + fields::mk(argv[3])));
        auto stream(parsers::_streamname()
                    .match(argv[4])
                    .fatal("parsing stream name " + fields::mk(argv[4])));
        conn->removestream(clientio::CLIENTIO, job, stream)
            .fatal(fields::mk("finishing stream")); }
    else errx(1, "unknown mode %s", argv[2]);

    delete conn;
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging(); }
