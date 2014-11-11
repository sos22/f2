#include "storageslave.H"

#include "jobname.H"
#include "logging.H"
#include "nnp.H"
#include "parsers.H"
#include "proto.H"
#include "serialise.H"
#include "storage.H"
#include "streamname.H"
#include "streamstatus.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "parsers.tmpl"
#include "rpcservice2.tmpl"

orerror<void>
storageslave::called(
    clientio,
    onconnectionthread oct,
    deserialise1 &ds,
    interfacetype type,
    nnp<incompletecall> ic) {
    /* rpcservice2 should enforce this for us, since we only claim to
     * support one interface type. */
    assert(type == interfacetype::storage);
    proto::storage::tag tag(ds);
    if (tag == proto::storage::tag::createempty) {
        jobname j(ds);
        streamname s(ds);
        if (!ds.isfailure()) ic->complete(createempty(j, s), oct); }
    else if (tag == proto::storage::tag::append) {
        jobname job(ds);
        streamname stream(ds);
        buffer bytes(ds);
        if (!ds.isfailure()) ic->complete(append(job, stream, bytes), oct); }
    else if (tag == proto::storage::tag::finish) {
        jobname job(ds);
        streamname stream(ds);
        if (!ds.isfailure()) ic->complete(finish(job, stream), oct); }
    else if (tag == proto::storage::tag::read) {
        jobname job(ds);
        streamname stream(ds);
        maybe<unsigned long> _start(ds);
        maybe<unsigned long> end(ds);
        if (!ds.isfailure()) {
            auto r(read(job, stream, _start.dflt(0), end.dflt(UINT64_MAX),
                        ic, oct));
            if (r.isfailure()) ds.fail(r.failure()); } }
    else if (tag == proto::storage::tag::listjobs) {
        maybe<jobname> _start(ds);
        maybe<unsigned> limit(ds);
        if (!ds.isfailure()) {
            auto r(listjobs(_start, limit, ic, oct));
            if (r.isfailure()) ds.fail(r.failure()); } }
    else if (tag == proto::storage::tag::liststreams) {
        jobname job(ds);
        maybe<streamname> cursor(ds);
        maybe<unsigned> limit(ds);
        if (!ds.isfailure()) {
            auto r(liststreams(job, cursor, limit, ic, oct));
            if (r.isfailure()) ds.fail(r.failure()); } }
    else if (tag == proto::storage::tag::removestream) {
        jobname job(ds);
        streamname stream(ds);
        if (!ds.isfailure()) ic->complete(removestream(job, stream), oct); }
    else ds.fail(error::invalidmessage);
    return ds.status(); }

orerror<nnp<storageslave> >
storageslave::build(clientio io,
                    const storageconfig &config) {
    return rpcservice2::listen<storageslave>(
        io,
        peername::all(peername::port::any),
        config); }

orerror<void>
storageslave::initialise(clientio) {
    auto b(beaconserver::build(config.beacon,
                               mklist(interfacetype::storage),
                               port()));
    if (b.isfailure()) return b.failure();
    beacon = b.success();
    return Success; }

storageslave::storageslave(const constoken &token,
                           const storageconfig &_config)
    : rpcservice2(token, interfacetype::storage),
      config(_config),
      mux(),
      beacon(NULL) {}

/* XXX this can sometimes leave stuff behind after a partial
 * failure. */
orerror<void>
storageslave::createempty(const jobname &t, const streamname &sn) {
    logmsg(loglevel::debug,
           "create empty output " + fields::mk(t) + " " + fields::mk(sn));
    filename jobdir(config.poolpath + t.asfilename());
    {   auto r(jobdir.mkdir());
        if (r.isfailure() && r != error::already) return r.failure(); }
    filename streamdir(jobdir + sn.asfilename());
    {   auto r(streamdir.mkdir());
        if (r.isfailure() && r != error::already) return r.failure(); }
    filename content(streamdir + "content");
    filename finished(streamdir + "finished");
    auto cexists(content.exists());
    if (cexists.isfailure()) return cexists.failure();
    auto fexists(finished.exists());
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
    buffer &b) {
    filename dirname(config.poolpath + jn.asfilename() + sn.asfilename());
    auto finished((dirname + "finished").exists());
    if (finished.isfailure()) return finished.failure();
    else if (finished == true) return error::toolate;
    filename content(dirname + "content");
    auto fd(content.openappend());
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
    {   auto content((dirname + "content").exists());
        if (content.isfailure()) return content.failure();
        else if (content == false) return error::notfound; }
    filename finished(dirname + "finished");
    auto exists(finished.exists());
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
    onconnectionthread oct) const {
    if (start > end) return error::invalidparameter;
    filename dirname(config.poolpath + jn.asfilename() + sn.asfilename());
    {   auto finished((dirname + "finished").exists());
        if (finished.isfailure()) return finished.failure();
        else if (finished == false) return error::toosoon; }
    auto content(dirname + "content");
    {   auto r(content.exists());
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
                 oct);
    return Success; }

orerror<void>
storageslave::listjobs(
    const maybe<jobname> &cursor,
    const maybe<unsigned> &limit,
    nnp<incompletecall> ic,
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
                 oct);
    return Success; }

orerror<void>
storageslave::liststreams(
    const jobname &jn,
    const maybe<streamname> &cursor,
    const maybe<unsigned> &limit,
    nnp<incompletecall> ic,
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
            auto finished((fname + "finished").exists());
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
    streamdir.rmdir()
        .warn("cannot remove " + fields::mk(streamdir));
    {   auto r(jobdir.rmdir());
        if (r != error::notempty) {
            r.warn("cannot remove " + fields::mk(jobdir)); } }
    if (already) return error::already;
    else return Success; }

void
storageslave::destroying(clientio io) {
    beacon->destroy(io);
    beacon = NULL; }
