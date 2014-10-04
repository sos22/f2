#include "storageslave.H"

#include "jobname.H"
#include "logging.H"
#include "parsers.H"
#include "proto.H"
#include "streamname.H"
#include "streamstatus.H"

#include "list.tmpl"
#include "parsers.tmpl"
#include "rpcservice.tmpl"

void
storageslave::call(const wireproto::rx_message &rxm, response *resp) {
    if (rxm.tag() == proto::CREATEEMPTY::tag) {
        auto job(rxm.getparam(proto::CREATEEMPTY::req::job));
        auto stream(rxm.getparam(proto::CREATEEMPTY::req::stream));
        if (!job || !stream) resp->fail(error::missingparameter);
        else resp->complete(createempty(job.just(), stream.just())); }
    else if (rxm.tag() == proto::APPEND::tag) {
        auto job(rxm.getparam(proto::APPEND::req::job));
        auto stream(rxm.getparam(proto::APPEND::req::stream));
        auto bytes(rxm.getparam(proto::APPEND::req::bytes));
        if (!job || !stream || !bytes) resp->fail(error::missingparameter);
        else if (bytes.just().empty()) resp->fail(error::invalidparameter);
        else resp->complete(append(job.just(), stream.just(), bytes.just())); }
    else if (rxm.tag() == proto::FINISH::tag) {
        auto job(rxm.getparam(proto::FINISH::req::job));
        auto stream(rxm.getparam(proto::FINISH::req::stream));
        if (!job || !stream) resp->fail(error::missingparameter);
        else resp->complete(finish(job.just(), stream.just())); }
    else if (rxm.tag() == proto::READ::tag) {
        auto job(rxm.getparam(proto::READ::req::job));
        auto stream(rxm.getparam(proto::READ::req::stream));
        if (!job || !stream) resp->fail(error::missingparameter);
        else read(
            resp,
            job.just(),
            stream.just(),
            rxm.getparam(proto::READ::req::start).dflt(0),
            rxm.getparam(proto::READ::req::end).dflt(UINT64_MAX)); }
    else if (rxm.tag() == proto::LISTJOBS::tag) {
        listjobs(
            resp,
            rxm.getparam(proto::LISTJOBS::req::cursor),
            rxm.getparam(proto::LISTJOBS::req::limit)); }
    else if (rxm.tag() == proto::LISTSTREAMS::tag) {
        auto jn(rxm.getparam(proto::LISTSTREAMS::req::job));
        if (!jn) resp->fail(error::missingparameter);
        else liststreams(
            resp,
            jn.just(),
            rxm.getparam(proto::LISTSTREAMS::req::cursor),
            rxm.getparam(proto::LISTSTREAMS::req::limit)); }
    else if (rxm.tag() == proto::REMOVESTREAM::tag) {
        auto jn(rxm.getparam(proto::REMOVESTREAM::req::job));
        auto sn(rxm.getparam(proto::REMOVESTREAM::req::stream));
        if (!jn || !sn) resp->fail(error::missingparameter);
        else resp->complete(removestream(jn.just(), sn.just())); }
    else resp->fail(error::unrecognisedmessage); }

storageslave::controliface::controliface(storageslave *_owner,
                                         controlserver *cs)
    : controlinterface(cs),
      owner(_owner) {
    start(); }

void
storageslave::controliface::getstatus(rpcservice::response *resp) const {
    resp->addparam(proto::STATUS::resp::storageslave, owner->status()); }

void
storageslave::controliface::getlistening(rpcservice::response *resp) const {
    resp->addparam(proto::LISTENING::resp::storageslave, owner->localname()); }


orerror<storageslave *>
storageslave::build(clientio io,
                    const storageconfig &config,
                    controlserver *cs) {
    return rpcservice::listen<storageslave>(
        io,
        peername::all(peername::port::any),
        cs,
        config); }

orerror<void>
storageslave::initialise(clientio) {
    auto b(beaconserver::build(config.beacon,
                               actortype::storageslave,
                               localname().getport(),
                               cs));
    if (b.isfailure()) return b.failure();
    beacon = b.success();
    return Success; }

storageslave::storageslave(const constoken &token,
                           controlserver *_cs,
                           const storageconfig &_config)
    : rpcservice(token),
      config(_config),
      mux(),
      beacon(NULL),
      cs(_cs),
      control_(this, cs) { }

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

void
storageslave::read(
    response *resp,
    const jobname &jn,
    const streamname &sn,
    uint64_t start,
    uint64_t end) const {
    if (start > end) {
        resp->fail(error::invalidparameter);
        return; }
    filename dirname(config.poolpath + jn.asfilename() + sn.asfilename());
    {   auto finished((dirname + "finished").exists());
        if (finished.isfailure()) {
            resp->fail(finished.failure());
            return; }
        else if (finished == false) {
            resp->fail(error::toosoon);
            return; } }
    auto content(dirname + "content");
    {   auto r(content.exists());
        if (r.isfailure()) {
            resp->fail(r.failure());
            return; }
        else if (r == false) {
            resp->fail(error::toosoon);
            return; } }
    auto sz(content.size());
    if (sz.isfailure()) {
        resp->fail(sz.failure());
        return; }
    if (start > sz.success()) start = sz.success();
    if (end > sz.success()) end = sz.success();
    resp->addparam(proto::READ::resp::size, sz.success());
    auto b(content.read(start, end));
    if (b.isfailure()) {
        resp->fail(b.failure());
        return; }
    resp->addparam(proto::READ::resp::bytes, b.success().steal());
    resp->complete(); }

void
storageslave::listjobs(
    response *resp,
    const maybe<jobname> &cursor,
    const maybe<unsigned> &limit) const {
    if (limit == 0) {
        resp->fail(error::invalidparameter);
        return; }
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
        if (it.isfailure()) {
            it.failure().warn("listing " + fields::mk(config.poolpath));
            resp->fail(it.failure());
            return; } }
    sort(res);
    maybe<jobname> newcursor(Nothing);
    if (limit != Nothing) {
        auto it(res.start());
        unsigned n;
        for (n = 0; n < limit.just() && !it.finished(); n++) it.next();
        if (!it.finished()) newcursor = *it;
        while (!it.finished()) it.remove(); }
    resp->addparam(proto::LISTJOBS::resp::cursor, newcursor);
    resp->addparam(proto::LISTJOBS::resp::jobs, res);
    resp->complete(); }

void
storageslave::liststreams(
    response *resp,
    const jobname &jn,
    const maybe<streamname> &cursor,
    const maybe<unsigned> &limit) const {
    if (limit == 0) {
        resp->fail(error::invalidparameter);
        return; }
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
            if (size.isfailure()) {
                size.failure().warn("getting size of " +
                                    fields::mk(fname + "content"));
                resp->fail(size.failure());
                return; }
            auto finished((fname + "finished").exists());
            if (finished.isfailure()) {
                finished.failure().warn("checking whether " + fields::mk(jn) +
                                        "::" + fields::mk(it.filename()) +
                                        " finished");
                resp->fail(finished.failure());
                return; }
            if (finished == true) {
                res.pushtail(streamstatus::finished(sn.success(),
                                                    size.success())); }
            else {
                res.pushtail(streamstatus::partial(sn.success(),
                                                   size.success())); } }
        if (it.isfailure()) {
            it.failure().warn("listing " + fields::mk(dir));
            resp->fail(it.failure());
            return; } }
    sort(res);
    maybe<streamname> newcursor(Nothing);
    if (limit != Nothing) {
        auto it(res.start());
        unsigned n;
        for (n = 0; n < limit.just() && !it.finished(); n++) it.next();
        if (!it.finished()) newcursor = it->name;
        while (!it.finished()) it.remove(); }
    resp->addparam(proto::LISTSTREAMS::resp::cursor, newcursor);
    resp->addparam(proto::LISTSTREAMS::resp::streams, res);
    resp->complete(); }

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
