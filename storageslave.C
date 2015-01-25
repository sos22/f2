#include "storageslave.H"

#include "buffer.H"
#include "bytecount.H"
#include "eqserver.H"
#include "jobname.H"
#include "logging.H"
#include "nnp.H"
#include "parsers.H"
#include "proto2.H"
#include "serialise.H"
#include "storage.H"
#include "streamname.H"
#include "streamstatus.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "parsers.tmpl"
#include "rpcservice2.tmpl"

orerror<nnp<storageslave> >
storageslave::build(clientio io,
                    const storageconfig &config) {
    return rpcservice2::listen<storageslave>(
        io,
        config.beacon.cluster,
        config.beacon.name,
        peername::all(peername::port::any),
        config); }

storageslave::storageslave(const constoken &token,
                           const storageconfig &_config)
    : rpcservice2(token, list<interfacetype>::mk(interfacetype::storage,
                                                 interfacetype::eq)),
      config(_config),
      mux(),
      eqs(*eqserver::build()),
      eqq(*eqs.mkqueue(proto::eq::names::storage)) {}

void
storageslave::destroy(clientio io) {
    eqq.destroy(io);
    delete this; }

storageslave::~storageslave() { eqs.destroy(); }

orerror<void>
storageslave::called(
    clientio io,
    deserialise1 &ds,
    interfacetype type,
    nnp<incompletecall> ic,
    onconnectionthread oct) {
    /* rpcservice2 should enforce this for us, since we only claim to
     * support the two interface types. */
    assert(type == interfacetype::storage || type == interfacetype::eq);
    if (type == interfacetype::eq) return eqs.called(io, ds, ic, oct);
    proto::storage::tag tag(ds);
    if (tag == proto::storage::tag::createjob) {
        jobname j(ds);
        if (!ds.isfailure()) {
            auto r(createjob(j));
            if (!r.isfailure()) eqq.queue(proto::storage::event::newjob(j), io);
            ic->complete(r, io, oct); } }
    else if (tag == proto::storage::tag::createstream) {
        jobname j(ds);
        streamname s(ds);
        if (!ds.isfailure()) {
            auto r(createstream(j, s));
            if (!r.isfailure()) {
                eqq.queue(proto::storage::event::newstream(j, s), io); }
            ic->complete(r, io, oct); } }
    else if (tag == proto::storage::tag::append) {
        jobname job(ds);
        streamname stream(ds);
        bytecount oldsize(ds);
        buffer bytes(ds);
        if (!ds.isfailure()) {
            ic->complete(append(job, stream, oldsize, bytes), io, oct); } }
    else if (tag == proto::storage::tag::finish) {
        jobname job(ds);
        streamname stream(ds);
        if (!ds.isfailure()) {
            auto r(finish(job, stream));
            if (!r.isfailure()) {
                eqq.queue(proto::storage::event::finishstream(job, stream),
                          io); }
            ic->complete(r, io, oct); } }
    else if (tag == proto::storage::tag::read) {
        jobname job(ds);
        streamname stream(ds);
        maybe<unsigned long> _start(ds);
        maybe<unsigned long> end(ds);
        if (!ds.isfailure()) {
            auto r(read(job, stream, _start.dflt(0), end.dflt(UINT64_MAX),
                        ic, io, oct));
            if (r.isfailure()) ds.fail(r.failure()); } }
    else if (tag == proto::storage::tag::listjobs) {
        maybe<jobname> _start(ds);
        maybe<unsigned> limit(ds);
        if (!ds.isfailure()) {
            auto r(listjobs(_start, limit, ic, io, oct));
            if (r.isfailure()) ds.fail(r.failure()); } }
    else if (tag == proto::storage::tag::liststreams) {
        jobname job(ds);
        maybe<streamname> cursor(ds);
        maybe<unsigned> limit(ds);
        if (!ds.isfailure()) {
            auto r(liststreams(job, cursor, limit, ic, io, oct));
            if (r.isfailure()) ds.fail(r.failure()); } }
    else if (tag == proto::storage::tag::removestream) {
        jobname job(ds);
        streamname stream(ds);
        if (!ds.isfailure()) {
            auto r(removestream(job, stream));
            if (!r.isfailure()) {
                eqq.queue(proto::storage::event::removestream(job, stream),
                          io); }
            ic->complete(r, io, oct); } }
    else if (tag == proto::storage::tag::removejob) {
        jobname job(ds);
        if (!ds.isfailure()) {
            auto r(removejob(job));
            if (!r.isfailure()) {
                eqq.queue(proto::storage::event::removejob(job), io); }
            ic->complete(r, io, oct); } }
    else ds.fail(error::invalidmessage);
    return ds.status(); }

orerror<void>
storageslave::createjob(const jobname &t) {
    logmsg(loglevel::debug, "create job " + fields::mk(t));
    return (config.poolpath + t.asfilename()).mkdir(); }

/* XXX this can sometimes leave stuff behind after a partial
 * failure. */
orerror<void>
storageslave::createstream(const jobname &t, const streamname &sn) {
    logmsg(loglevel::debug,
           "create empty output " + fields::mk(t) + " " + fields::mk(sn));
    filename jobdir(config.poolpath + t.asfilename());
    {   auto r(jobdir.isdir());
        if (r.isfailure()) return r.failure();
        else if (!r.success()) return error::toosoon; }
    filename streamdir(jobdir + sn.asfilename());
    {   auto r(streamdir.mkdir());
        if (r.isfailure() && r != error::already) return r.failure(); }
    filename content(streamdir + "content");
    filename finished(streamdir + "finished");
    auto cexists(content.isfile());
    if (cexists.isfailure()) return cexists.failure();
    auto fexists(finished.isfile());
    if (fexists.isfailure()) return fexists.failure();
    if (cexists == true && fexists == true) {
        /* Already have job results -> tell caller to stop */
        return error::already; }
    /* Remove any leftover finished marker. */
    if (fexists == true) {
        auto r(finished.unlink());
        if (r != Success && r != error::already) return r.failure(); }
    /* Create the new content file. */
    {   auto r(content.createfile());
        if (r == Success || r == error::already) return Success; }
    {   auto r(content.unlink());
        if (r.isfailure()) return r.failure(); }
    return content.createfile(); }

orerror<void>
storageslave::append(
    const jobname &jn,
    const streamname &sn,
    bytecount oldsize,
    buffer &b) {
    filename dirname(config.poolpath + jn.asfilename() + sn.asfilename());
    auto finished((dirname + "finished").isfile());
    if (finished.isfailure()) return finished.failure();
    else if (finished == true) return error::toolate;
    filename content(dirname + "content");
    auto fd(content.openappend(oldsize));
    if (fd.isfailure()) return fd.failure();
    unsigned long initialavail(b.avail());
    while (!b.empty()) {
        subscriber sub;
        auto r(b.send(
                   /* We know the FD points at a local file, not a
                      network socket, so we don't have to worry about
                      network deadlocks. */
                   clientio::CLIENTIO,
                   fd.success(),
                   sub));
        if (r.isfailure()) {
            if (initialavail != b.avail()) {
                logmsg(loglevel::error,
                       "failed writing to " + fields::mk(jn) +
                       " " + fields::mk(sn) + " after " +
                       fields::mk(initialavail - b.avail()) + " of " +
                       fields::mk(initialavail) + ": " +
                       fields::mk(r.failure()));
            } else {
                logmsg(loglevel::failure,
                       "failed writing to " + fields::mk(jn) +
                       " " + fields::mk(sn) + ": " +
                       fields::mk(r.failure())); }
            fd.success().close();
            return r.failure(); }
        assert(r.success() == NULL); }
    fd.success().close();
    return Success; }

orerror<void>
storageslave::finish(
    const jobname &jn,
    const streamname &sn) {
    filename dirname(config.poolpath + jn.asfilename() + sn.asfilename());
    {   auto content((dirname + "content").isfile());
        if (content.isfailure()) return content.failure();
        else if (content == false) return error::notfound; }
    filename finished(dirname + "finished");
    auto exists(finished.isfile());
    if (exists.isfailure()) return exists.failure();
    else if (exists == true) return error::already;
    return finished.createfile(); }

orerror<void>
storageslave::read(
    const jobname &jn,
    const streamname &sn,
    uint64_t start,
    uint64_t end,
    nnp<incompletecall> ic,
    acquirestxlock atl,
    onconnectionthread oct) const {
    if (start > end) return error::invalidparameter;
    filename dirname(config.poolpath + jn.asfilename() + sn.asfilename());
    {   auto finished((dirname + "finished").isfile());
        if (finished.isfailure()) return finished.failure();
        else if (finished == false) return error::toosoon; }
    auto content(dirname + "content");
    {   auto r(content.isfile());
        if (r.isfailure()) return r.failure();
        else if (r == false) return error::toosoon; }
    auto sz(content.size());
    if (sz.isfailure()) return sz.failure();
    if (start > sz.success()) start = sz.success();
    if (end > sz.success()) end = sz.success();
    auto b(content.read(start, end));
    if (b.isfailure()) return b.failure();
    ic->complete([sz, &b]
                 (serialise1 &s,
                  mutex_t::token /* txlock */,
                  onconnectionthread) {
                     s.push(sz.success());
                     b.success().serialise(s); },
                 atl,
                 oct);
    return Success; }

orerror<void>
storageslave::listjobs(
    const maybe<jobname> &cursor,
    const maybe<unsigned> &limit,
    nnp<incompletecall> ic,
    acquirestxlock atl,
    onconnectionthread oct) const {
    auto &parser(parsers::_jobname());
    list<jobname> res;
    {   filename::diriter it(config.poolpath);
        for (/**/;
                 !it.isfailure() && !it.finished();
                 it.next()) {
            if (strcmp(it.filename(), ".") == 0 ||
                strcmp(it.filename(), "..") == 0) {
                continue; }
            auto jn(parser.match(it.filename()));
            if (jn.isfailure()) {
                jn.failure().warn(
                    "cannot parse " +
                    fields::mk(config.poolpath + it.filename()) +
                    " as job name");
                continue; }
            if (cursor != Nothing && jn.success() < cursor.just()) continue;
            res.pushtail(jn.success()); }
        if (it.isfailure()) return it.failure(); }
    sort(res);
    maybe<jobname> newcursor(Nothing);
    if (limit != Nothing) {
        auto it(res.start());
        unsigned n;
        for (n = 0; n < limit.just() && !it.finished(); n++) it.next();
        if (!it.finished()) newcursor = *it;
        while (!it.finished()) it.remove(); }
    ic->complete([&newcursor, &res]
                 (serialise1 &s,
                  mutex_t::token /* txlock */,
                  onconnectionthread) {
                     newcursor.serialise(s);
                     res.serialise(s); },
                 atl,
                 oct);
    return Success; }

orerror<void>
storageslave::liststreams(
    const jobname &jn,
    const maybe<streamname> &cursor,
    const maybe<unsigned> &limit,
    nnp<incompletecall> ic,
    acquirestxlock atl,
    onconnectionthread oct) const {
    auto dir(config.poolpath + jn.asfilename());
    const parser<streamname> &parser(parsers::_streamname());
    list<streamstatus> res;
    {   filename::diriter it(dir);
        for (/**/;
                 !it.isfailure() && !it.finished();
                 it.next()) {
            if (strcmp(it.filename(), ".") == 0 ||
                strcmp(it.filename(), "..") == 0) {
                continue; }
            auto sn(parser.match(it.filename()));
            if (sn.isfailure()) {
                sn.failure().warn(
                    "cannot parse " + fields::mk(dir + it.filename()) +
                    " as stream name");
                continue; }
            if (cursor != Nothing && sn.success() < cursor.just()) continue;
            auto fname(dir + it.filename());
            auto size((fname + "content").size());
            if (size.isfailure()) return size.failure();
            auto finished((fname + "finished").isfile());
            if (finished.isfailure()) return finished.failure();
            if (finished == true) {
                res.pushtail(streamstatus::finished(sn.success(),
                                                    size.success())); }
            else {
                res.pushtail(streamstatus::partial(sn.success(),
                                                   size.success())); } }
        if (it.isfailure()) return it.failure(); }
    sort(res);
    maybe<streamname> newcursor(Nothing);
    if (limit != Nothing) {
        auto it(res.start());
        unsigned n;
        for (n = 0; n < limit.just() && !it.finished(); n++) it.next();
        if (!it.finished()) newcursor = it->name;
        while (!it.finished()) it.remove(); }
    ic->complete(
        [&newcursor, &res]
        (serialise1 &s, mutex_t::token /* txlock */, onconnectionthread) {
            newcursor.serialise(s);
            res.serialise(s); },
        atl,
        oct);
    return Success; }

orerror<void>
storageslave::removestream(const jobname &jn,
                           const streamname &sn) {
    auto jobdir(config.poolpath + jn.asfilename());
    auto streamdir(jobdir + sn.asfilename());
    bool already;
    {   auto r((streamdir + "content").unlink());
        if (r != Success && r != error::already) return r.failure();
        already = r == error::already; }
    {   auto r((streamdir + "finished").unlink());
        if (r != Success && r != error::already) return r.failure(); }
    streamdir.rmdir().warn("cannot remove " + fields::mk(streamdir));
    if (already) return error::already;
    else return Success; }

orerror<void>
storageslave::removejob(const jobname &jn) {
    logmsg(loglevel::debug, "remove job " + fields::mk(jn));
    return (config.poolpath + jn.asfilename()).rmdir(); }
