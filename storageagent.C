/* XXX this can sometimes leave stuff behind after a partial
 * failure. */

#include "storageagent.H"

#include "buffer.H"
#include "bytecount.H"
#include "eqserver.H"
#include "job.H"
#include "jobname.H"
#include "logging.H"
#include "nnp.H"
#include "parsers.H"
#include "proto2.H"
#include "serialise.H"
#include "storage.H"
#include "streamname.H"
#include "streamstatus.H"

#include "fields.tmpl"
#include "list.tmpl"
#include "maybe.tmpl"
#include "mutex.tmpl"
#include "orerror.tmpl"
#include "parsers.tmpl"
#include "rpcservice2.tmpl"

orerror<void>
storageagent::format(const filename &fn) {
    orerror<void> r(fn.mkdir());
    if (r == error::already) {
        /* Replacing an empty directory is allowed (if for no reason
         * other than crash recovery). */
        filename::diriter it(fn);
        if (it.isfailure()) return it.failure();
        if (!it.finished()) return error::already; }
    /* Creating the queue is the commit point. */
    r = eqserver::formatqueue(proto::eq::names::storage, fn + "queue");
    if (r.isfailure()) {
        fn.rmdir()
            .warn("cannot remove partial storage pool " +
                  fields::mk(fn)); }
    return r; }

orerror<nnp<storageagent> >
storageagent::build(clientio io, const storageconfig &config) {
    auto eqs(eqserver::build());
    auto eqq(eqs->openqueue(proto::eq::names::storage,
                            config.poolpath + "queue"));
    if (eqq.isfailure()) {
        eqs->destroy();
        return eqq.failure(); }
    return rpcservice2::listen<storageagent>(
        io,
        config.beacon.cluster,
        config.beacon.name,
        peername::all(peername::port::any),
        config,
        *eqs,
        *eqq.success()); }

orerror<nnp<storageagent> >
storageagent::build(clientio io,
                    const clustername &cluster,
                    const agentname &an,
                    const filename &pool) {
    return build(io, storageconfig(pool,
                                   beaconserverconfig::dflt(cluster, an))); }

storageagent::storageagent(const constoken &token,
                           const storageconfig &_config,
                           eqserver &_eqs,
                           eventqueue<proto::storage::event> &_eqq)
    : rpcservice2(token, list<interfacetype>::mk(interfacetype::storage,
                                                 interfacetype::eq)),
      config(_config),
      mux(),
      eqs(_eqs),
      eqq(_eqq) {}

orerror<void>
storageagent::initialise(clientio) {
    /* Check for jobs which got half constructed before we crashed and
     * remove them. */
    filename::diriter it(config.poolpath);
    for (/**/; !it.finished(); it.next()) {
        if (!strcmp(it.filename(), "queue")) continue;
        auto jobname(config.poolpath + it.filename());
        auto complete(jobname + "complete");
        auto r(complete.isfile());
        if (r.isfailure()) return r.failure();
        if (r.success() == false) {
            logmsg(loglevel::notice,
                   "remove incomplete job " +
                   jobname.field() +
                   " for crash recovery");
            auto r2((config.poolpath + it.filename()).rmtree());
            r2.warn("failed to remove " + jobname.field());
            if (r2.isfailure()) return r2; } }
    if (it.isfailure()) return it.failure();
    else return Success; }

void
storageagent::destroying(clientio io) { eqq.destroy(io); }

storageagent::~storageagent() { eqs.destroy(); }

orerror<void>
storageagent::called(
    clientio io,
    deserialise1 &ds,
    interfacetype type,
    nnp<incompletecall> ic,
    onconnectionthread oct) {
    return mux.locked<orerror<void> >([&] (mutex_t::token tok) {
            return _called(io, ds, type, ic, oct, tok); }); }

orerror<void>
storageagent::_called(
    clientio io,
    deserialise1 &ds,
    interfacetype type,
    nnp<incompletecall> ic,
    onconnectionthread oct,
    mutex_t::token /* storageagent lock */) {
    /* rpcservice2 should enforce this for us, since we only claim to
     * support the two interface types. */
    assert(type == interfacetype::storage || type == interfacetype::eq);
    if (type == interfacetype::eq) return eqs.called(io, ds, ic, oct);
    proto::storage::tag tag(ds);
    if (tag == proto::storage::tag::createjob) {
        job j(ds);
        if (ds.isfailure()) return ds.failure();
        auto r(createjob(io, j));
        if (r.isfailure()) return r.failure();
        ic->complete([eid(r.success())] (serialise1 &s,
                                         mutex_t::token /* txlock */,
                                         onconnectionthread) {
                         s.push(eid); },
                     acquirestxlock(io),
                     oct);
        return Success; }
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
        if (ds.isfailure()) return ds.failure();
        auto r(finish(io, job, stream));
        if (r.isfailure()) return r.failure();
        ic->complete([eid(r.success())] (serialise1 &s,
                                         mutex_t::token /* txlock */,
                                         onconnectionthread) {
                         s.push(eid); },
                     acquirestxlock(io),
                     oct);
        return Success; }
    else if (tag == proto::storage::tag::read) {
        jobname job(ds);
        streamname stream(ds);
        maybe<bytecount> _start(ds);
        maybe<bytecount> end(ds);
        if (!ds.isfailure()) {
            auto r(read(job, stream, _start.dflt(0_B),
                        end.dflt(bytecount::bytes(UINT64_MAX)),
                        ic, io, oct));
            if (r.isfailure()) ds.fail(r.failure()); } }
    else if (tag == proto::storage::tag::listjobs) {
        if (!ds.isfailure()) {
            auto r(listjobs(ic, io, oct));
            if (r.isfailure()) ds.fail(r.failure()); } }
    else if (tag == proto::storage::tag::statjob) {
        jobname j(ds);
        if (!ds.isfailure()) {
            auto r(statjob(j, ic, io, oct));
            if (r.isfailure()) ds.fail(r.failure()); } }
    else if (tag == proto::storage::tag::liststreams) {
        jobname job(ds);
        if (!ds.isfailure()) {
            auto r(liststreams(job, ic, io, oct));
            if (r.isfailure()) ds.fail(r.failure()); } }
    else if (tag == proto::storage::tag::statstream) {
        jobname job(ds);
        streamname sn(ds);
        if (!ds.isfailure()) {
            auto r(statstream(job, sn));
            if (r.isfailure()) ds.fail(r.failure());
            else ic->complete([&r] (serialise1 &s,
                                    mutex_t::token /* txlock */,
                                    onconnectionthread) {
                                  s.push(r.success()); },
                              io,
                              oct); } }
    else if (tag == proto::storage::tag::removejob) {
        jobname job(ds);
        if (ds.isfailure()) return ds.failure();
        auto r(removejob(io, job));
        if (r.isfailure()) return r.failure();
        ic->complete([eid(r.success())] (serialise1 &s,
                                         mutex_t::token /* txlock */,
                                         onconnectionthread) {
                         s.push(eid); },
                     acquirestxlock(io),
                     oct);
        return Success; }
    else ds.fail(error::invalidmessage);
    return ds.status(); }

orerror<proto::eq::eventid>
storageagent::createjob(clientio io, const job &t) {
    logmsg(loglevel::debug, "create job " + fields::mk(t));
    auto dirname(config.poolpath + t.name().asfilename());
    orerror<void> r(dirname.mkdir());
    if (r == error::already) {
        /* If it's a leftover from a previous operation remove it and
         * try again. */
        logmsg(loglevel::debug, "clear out stale entry");
        auto complete((dirname + "complete").isfile());
        if (complete.isfailure()) goto fail;
        else if (complete == false) {
            r = dirname.rmtree();
            if (r.isfailure()) goto fail;
            r = dirname.mkdir();
            if (r.isfailure()) goto fail; }
        else {
            /* Check for replays. */
            /* Job must match */
            auto prevjob((dirname + "job").deserialiseobj<job>());
            if (prevjob.isfailure() || prevjob.success() != t) goto fail;
            /* Every output must be present and empty. */
            for (auto it(t.outputs().start()); !it.finished(); it.next()) {
                auto fn(dirname + it->asfilename());
                if ((dirname + it->asfilename() + "content").size() != 0_B) {
                    goto fail; }
                filename::diriter it2(fn);
                if (it2.finished() || strcmp(it2.filename(), "content")) {
                    goto fail; }
                it2.next();
                if (!it2.finished() || it2.isfailure()) goto fail; }
            /* Everything present must match one of the outputs. */
            filename::diriter it(dirname);
            auto &parser(streamname::parser());
            for (/**/; !it.finished(); it.next()) {
                auto fn(it.filename());
                if (!strcmp(fn, "job") || !strcmp(fn, "eid")) continue;
                auto sn(parser.match(fn));
                if (sn.isfailure() ||
                    !t.outputs().contains(sn.success())) {
                    goto fail; } }
            /* Looks like a replay -> succeeds without needing to do
             * anything. */
            auto eid((dirname + "eid").deserialiseobj<proto::eq::eventid>());
            if (eid.isfailure()) goto fail;
            logmsg(loglevel::debug, "replay");
            return eid; } }
    if (r.isfailure()) {
        r.failure().warn("creating job dir");
        return r.failure(); }
    r = (dirname + "job").serialiseobj(t);
    if (r.isfailure()) {
        r.failure().warn("serialising job");
        goto fail; }
    for (auto it(t.outputs().start()); !it.finished(); it.next()) {
        auto fn(dirname + it->asfilename());
        r = fn.mkdir();
        if (r.isfailure()) {
            r.failure().warn("output dir " + it->field());
            goto fail; }
        r = (fn + "content").createfile();
        if (r.isfailure()) {
            r.failure().warn("output content " + it->field());
            goto fail; } }
    {   auto eid(eqq.queue(proto::storage::event::newjob(t.name()), io));
        r = (dirname + "eid").serialiseobj(eid);
        if (r.isfailure()) {
            r.failure().warn("eid");
            goto fail; }
        r = (dirname + "complete").createfile();
        if (r.isfailure()) {
            r.failure().warn("complete");
            goto fail; }
        logmsg(loglevel::debug, "succeeded");
        return eid; }
 fail:
    r.failure().warn("creating job " + t.field());
    dirname
        .rmtree()
        .warn("removing partially constructed job " + dirname.field());
    return r.failure(); }


orerror<void>
storageagent::append(
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

orerror<proto::eq::eventid>
storageagent::finish(
    clientio io,
    const jobname &jn,
    const streamname &sn) {
    filename dirname(config.poolpath + jn.asfilename() + sn.asfilename());
    filename content(dirname + "content");
    {   auto t(content.isfile());
        if (t.isfailure()) return t.failure();
        else if (t == false) return error::notfound; }
    auto sz(content.size());
    if (sz.isfailure()) return sz.failure();
    filename finished(dirname + "finished");
    auto exists(finished.isfile());
    if (exists.isfailure()) return exists.failure();
    else if (exists == true) return error::already;
    {   auto t(finished.createfile());
        if (t.isfailure()) return t.failure(); }
    return eqq.queue(
        proto::storage::event::finishstream(
            jn,
            sn,
            streamstatus::finished(sn, sz.success())),
        io); }

orerror<void>
storageagent::read(
    const jobname &jn,
    const streamname &sn,
    bytecount start,
    bytecount end,
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
storageagent::listjobs(
    nnp<incompletecall> ic,
    acquirestxlock atl,
    onconnectionthread oct) const {
    auto &parser(jobname::parser());
    list<jobname> res;
    {   filename::diriter it(config.poolpath);
        for (/**/; !it.finished(); it.next()) {
            if (strcmp(it.filename(), "queue") == 0) continue;
            /* Ignore incomplete jobs. */
            {   auto r((config.poolpath + it.filename() + "complete")
                       .isfile());
                if (r.isfailure()) return r.failure();
                if (r == false) continue; }
            auto jn(parser.match(it.filename()));
            if (jn.isfailure()) {
                jn.failure().warn(
                    "cannot parse " +
                    fields::mk(config.poolpath + it.filename()) +
                    " as job name");
                continue; }
            res.pushtail(jn.success()); }
        if (it.isfailure()) return it.failure(); }
    sort(res);
    ic->complete([r = decltype(res)(Steal, res), this]
                 (serialise1 &s,
                  mutex_t::token /* txlock */,
                  onconnectionthread) {
                     s.push(proto::storage::listjobsres(
                                eqq.lastid(),
                                r)); },
                 atl,
                 oct);
    return Success; }

orerror<void>
storageagent::statjob(
    const jobname &jn,
    nnp<incompletecall> ic,
    acquirestxlock atl,
    onconnectionthread oct) const {
    auto r((config.poolpath + jn.asfilename() + "job")
           .deserialiseobj<job>());
    logmsg(loglevel::debug,
           "stat " + fields::mk(jn) +
           " -> " + fields::mk(r));
    if (r.issuccess()) {
        ic->complete([&r] (serialise1 &s,
                           mutex_t::token /* txlock */,
                           onconnectionthread) {
                         s.push(r.success()); },
                     atl,
                     oct); }
    return r; }

orerror<void>
storageagent::liststreams(
    const jobname &jn,
    nnp<incompletecall> ic,
    acquirestxlock atl,
    onconnectionthread oct) const {
    logmsg(loglevel::debug, "liststreams " + jn.field());
    auto dir(config.poolpath + jn.asfilename());
    auto &parser(streamname::filenameparser());
    list<streamstatus> res;
    {   filename::diriter it(dir);
        for (/**/; !it.finished(); it.next()) {
            auto fn(it.filename());
            if (strcmp(fn, "job") == 0 ||
                strcmp(fn, "eid") == 0 ||
                strcmp(fn, "complete") == 0) {
                continue; }
            auto sn(parser.match(fn));
            if (sn.isfailure()) {
                sn.failure().warn(
                    "cannot parse " + fields::mk(dir + it.filename()) +
                    " as stream name");
                continue; }
            auto fname(dir + fn);
            auto size((fname + "content").size());
            if (size.isfailure()) {
                size.failure().warn("sizing " + fields::mk(fname + "content"));
                return size.failure(); }
            auto finished((fname + "finished").isfile());
            if (finished.isfailure()) {
                finished.failure().warn("getting finished flag on " +
                                        fields::mk(fname));
                return finished.failure(); }
            if (finished == true) {
                res.pushtail(streamstatus::finished(sn.success(),
                                                    size.success())); }
            else {
                res.pushtail(streamstatus::partial(sn.success(),
                                                   size.success())); } }
        if (it.isfailure()) {
            it.failure().warn("listing " + fields::mk(dir));
            return it.failure(); } }
    sort<streamstatus>(
        res,
        [] (const streamstatus &a, const streamstatus &b) -> bool {
            return a.name() > b.name(); });
    logmsg(loglevel::debug, "liststreams " + jn.field() + " -> " + res.field());
    ic->complete(
        [&res, this]
        (serialise1 &s, mutex_t::token /* txlock */, onconnectionthread) {
            s.push(proto::storage::liststreamsres(eqq.lastid(), res)); },
        atl,
        oct);
    return Success; }

orerror<streamstatus>
storageagent::statstream(const jobname &jn,
                         const streamname &sn) {
    auto jobdir(config.poolpath + jn.asfilename());
    auto streamdir(jobdir + sn.asfilename());
    bool finished;
    {   auto t(streamdir + "finished");
        auto r(t.isfile());
        if (r.isfailure()) return r.failure();
        finished = r.success(); }
    bytecount sz(0_B);
    {   auto t(streamdir + "content");
        auto r(t.size());
        if (r.isfailure()) return r.failure();
        sz = r.success(); }
    if (finished) return streamstatus::finished(sn, sz);
    else return streamstatus::partial(sn, sz); }

orerror<proto::eq::eventid>
storageagent::removejob(clientio io, const jobname &jn) {
    logmsg(loglevel::debug, "remove job " + fields::mk(jn));
    auto dirname(config.poolpath + jn.asfilename());
    {   /* Start by removing the complete tag, so that crash recovery
         * will finish off the removal for us. */
        auto r((dirname + "complete").unlink());
        if (r.isfailure()) return r.failure(); }
    auto r(dirname.rmtree());
    if (r.isfailure()) {
        r.failure().warn(
            "remove job directory " + dirname.field() +
            " after marking it incomplete"); }
    return eqq.queue(proto::storage::event::removejob(jn), io); }
