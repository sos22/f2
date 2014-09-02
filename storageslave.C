#include "storageslave.H"

#include "beaconclient.H"
#include "filename.H"
#include "parsers.H"
#include "jobname.H"
#include "storageconfig.H"
#include "streamname.H"
#include "streamstatus.H"
#include "tcpsocket.H"

#include "filename.tmpl"
#include "list.tmpl"
#include "parsers.tmpl"
#include "rpcconn.tmpl"
#include "rpcserver.tmpl"
#include "wireproto.tmpl"

wireproto_wrapper_type(storageslave::status_t);

class storageslaveconn : public rpcconn {
    friend class thread;
    friend class pausedthread<storageslaveconn>;
private: storageslave *const owner;
private: bool const ismaster;
private: storageslaveconn(const rpcconn::rpcconntoken &tok,
                          storageslave *_owner,
                          bool _ismaster);
private: messageresult message(const wireproto::rx_message &);
private: void endconn(clientio);
};

storageslaveconn::storageslaveconn(
    const rpcconn::rpcconntoken &tok,
    storageslave *_owner,
    bool _ismaster)
    : rpcconn(tok),
      owner(_owner),
      ismaster(_ismaster) {
    auto token(owner->mux.lock());
    owner->clients.pushtail(this);
    owner->mux.unlock(&token); }

messageresult
storageslaveconn::message(const wireproto::rx_message &rxm) {
    if (rxm.tag() == proto::CREATEEMPTY::tag) {
        auto job(rxm.getparam(proto::CREATEEMPTY::req::job));
        auto stream(rxm.getparam(proto::CREATEEMPTY::req::stream));
        if (!job || !stream) return error::missingparameter;
        auto res(owner->createempty(job.just(), stream.just()));
        if (res.isfailure()) return res.failure();
        else return new wireproto::resp_message(rxm);
    } else if (rxm.tag() == proto::APPEND::tag) {
        auto job(rxm.getparam(proto::APPEND::req::job));
        auto stream(rxm.getparam(proto::APPEND::req::stream));
        auto bytes(rxm.getparam(proto::APPEND::req::bytes));
        if (!job || !stream || !bytes) return error::missingparameter;
        if (bytes.just().empty()) return error::invalidparameter;
        auto res(owner->append(job.just(), stream.just(), bytes.just()));
        if (res.isfailure()) return res.failure();
        else return new wireproto::resp_message(rxm);
    } else if (rxm.tag() == proto::FINISH::tag) {
        auto job(rxm.getparam(proto::FINISH::req::job));
        auto stream(rxm.getparam(proto::FINISH::req::stream));
        if (!job || !stream) return error::missingparameter;
        auto res(owner->finish(job.just(), stream.just()));
        if (res.isfailure()) return res.failure();
        else return new wireproto::resp_message(rxm);
    } else if (rxm.tag() == proto::READ::tag) {
        auto job(rxm.getparam(proto::READ::req::job));
        auto stream(rxm.getparam(proto::READ::req::stream));
        if (!job || !stream) return error::missingparameter;
        else return owner->read(
            rxm,
            job.just(),
            stream.just(),
            rxm.getparam(proto::READ::req::start).dflt(0),
            rxm.getparam(proto::READ::req::end).dflt(UINT64_MAX));
    } else if (rxm.tag() == proto::LISTJOBS::tag) {
        return owner->listjobs(
            rxm,
            rxm.getparam(proto::LISTJOBS::req::cursor),
            rxm.getparam(proto::LISTJOBS::req::limit));
    } else if (rxm.tag() == proto::LISTSTREAMS::tag) {
        auto jn(rxm.getparam(proto::LISTSTREAMS::req::job));
        if (!jn) return error::missingparameter;
        return owner->liststreams(
            rxm,
            jn.just(),
            rxm.getparam(proto::LISTSTREAMS::req::cursor),
            rxm.getparam(proto::LISTSTREAMS::req::limit));
    } else if (rxm.tag() == proto::REMOVESTREAM::tag) {
        auto jn(rxm.getparam(proto::REMOVESTREAM::req::job));
        auto sn(rxm.getparam(proto::REMOVESTREAM::req::stream));
        if (!jn || !sn) return error::missingparameter;
        auto res(owner->removestream(jn.just(), sn.just()));
        if (res.isfailure()) return res.failure();
        else return new wireproto::resp_message(rxm);
    } else {
        return rpcconn::message(rxm); } }

void
storageslaveconn::endconn(clientio) {
    if (ismaster) {
        logmsg(loglevel::failure, fields::mk("lost connection to master"));
        owner->keephouse.publish(); }
    auto token(owner->mux.lock());
    for (auto it(owner->clients.start()); true; it.next()) {
        if (*it == this) {
            it.remove();
            break; } }
    owner->mux.unlock(&token); }

void
storageslave::controliface::getstatus(wireproto::tx_message *msg) const {
    msg->addparam(proto::STATUS::resp::storageslave, owner->status()); }

void
storageslave::controliface::getlistening(wireproto::resp_message *msg) const {
    msg->addparam(proto::LISTENING::resp::storageslave, owner->localname()); }

orerror<storageslave *>
storageslave::build(clientio io,
                    const storageconfig &config,
                    controlserver *cs) {
    auto br(beaconclient(io, config.beacon));
    if (br.isfailure()) return br.failure();
    auto server(rpcserver::listen<storageslave>(
                    config.listenon,
                    br.success().secret,
                    cs,
                    config));
    if (server.isfailure()) return server.failure();
    auto mc(rpcconn::connectmaster<storageslaveconn>(
                io,
                br.success(),
                config.name,
                config.connconfig,
                server.success().unwrap(),
                true));
    if (mc.isfailure()) {
        server.success().destroy();
        return mc.failure(); }
    server.success().unwrap()->master.conn = mc.success();
    return server.success().go(); }

storageslave::storageslave(constoken token,
                           listenfd fd,
                           const registrationsecret &_rs,
                           controlserver *cs,
                           const storageconfig &_config)
    : rpcserver(token, fd),
      control_(this, cs),
      rs(_rs),
      master(),
      clients(),
      config(_config),
      mux() {
    control_.start(); }

orerror<rpcconn *>
storageslave::accept(socket_t s) {
    return rpcconn::fromsocket<storageslaveconn>(
        s,
        rpcconnauth::mksendhelloslavea(rs, config.name, config.connconfig),
        config.connconfig,
        this,
        false); }

/* Invoked from the RPC server thread when we lose the connection to
 * the master.  The server won't accept any additional connections
 * while this is running, despite the @io token, which isn't ideal but
 * is probably good enough for now. */
void
storageslave::housekeeping(clientio io) {
    logmsg(loglevel::error,
           fields::mk("This might go a very long time without checking for shutdown"));

    /* Start a teardown sequence.  Probably not actually necessary,
       because we're already pretty confident that it's dead, but
       might as well do it anyway. */
    master.conn->teardown();
    /* Prevent any more calls starting against it. */
    auto token(master.lock.lock());
    auto oldconn(master.conn);
    master.conn = NULL;
    bool d = master.dying;
    master.lock.unlock(&token);

    if (d) {
        /* slave is dying -> don't try to reconnect. */
        return; }

  retry:
    /* Re-run the beacon protocol, in case the master's moved. */
    auto br(beaconclient(io, config.beacon));
    while (br.isfailure()) {
        br.failure().warn("re-locating master following disconnection");
        br = beaconclient(io, config.beacon); }

    /* Wait for references to the old connection to drop away. */
    token = master.lock.lock();
    if (master.refcount != 0) {
        subscriber sub;
        subscription ss(sub, master.idle);
        while (master.refcount != 0) {
            master.lock.unlock(&token);
            sub.wait(io);
            token = master.lock.lock(); } }
    master.lock.unlock(&token);

    /* Finish tearing down the old connection. */
    oldconn->join(io);

    /* Reconnect */
    auto mc(rpcconn::connectmaster<storageslaveconn>(
                io,
                br.success(),
                config.name,
                config.connconfig,
                this,
                true));
    if (mc.isfailure()) {
        mc.failure().warn("reconnecting to master at " +
                          fields::mk(br.success().mastername) +
                          "; retrying");
        goto retry; }

    token = master.lock.lock();
    if (master.dying) {
        /* The slave is dying -> shouldn't re-establish master
           connection.  Tear it all back down. */
        master.lock.unlock(&token);
        mc.success()->destroy(io); }
    else {
        master.conn = mc.success();
        master.lock.unlock(&token); } }

/* XXX this can sometimes leave stuff behind after a partial
 * failure. */
orerror<void>
storageslave::createempty(const jobname &t, const streamname &sn) const {
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
    buffer &b) const {
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
    const streamname &sn) const {
    filename dirname(config.poolpath + jn.asfilename() + sn.asfilename());
    {   auto content((dirname + "content").exists());
        if (content.isfailure()) return content.failure();
        else if (content == false) return error::notfound; }
    filename finished(dirname + "finished");
    auto exists(finished.exists());
    if (exists.isfailure()) return exists.failure();
    else if (exists == true) return error::already;
    return finished.createfile(); }

messageresult
storageslave::read(
    const wireproto::rx_message &rxm,
    const jobname &jn,
    const streamname &sn,
    uint64_t start,
    uint64_t end) const {
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
    auto resp(new wireproto::resp_message(rxm));
    resp->addparam(proto::READ::resp::size, sz.success());
    auto b(content.read(start, end));
    if (b.isfailure()) {
        delete resp;
        return b.failure(); }
    resp->addparam(proto::READ::resp::bytes, b.success().steal());
    return resp; }

messageresult
storageslave::listjobs(
    const wireproto::rx_message &rxm,
    const maybe<jobname> &cursor,
    const maybe<unsigned> &limit) const {
    if (limit == 0) return error::invalidparameter;
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
            res.flush();
            return it.failure(); } }
    sort(res);
    maybe<jobname> newcursor(Nothing);
    if (limit != Nothing) {
        auto it(res.start());
        unsigned n;
        for (n = 0; n < limit.just() && !it.finished(); n++) it.next();
        if (!it.finished()) newcursor = *it;
        while (!it.finished()) it.remove(); }
    auto resp(new wireproto::resp_message(rxm));
    if (newcursor != Nothing) {
        resp->addparam(proto::LISTJOBS::resp::cursor, newcursor.just()); }
    resp->addparam(proto::LISTJOBS::resp::jobs, res);
    res.flush();
    return resp; }

messageresult
storageslave::liststreams(
    const wireproto::rx_message &rxm,
    const jobname &jn,
    const maybe<streamname> &cursor,
    const maybe<unsigned> &limit) const {
    if (limit == 0) return error::invalidparameter;
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
                res.flush();
                return size.failure(); }
            auto finished((fname + "finished").exists());
            if (finished.isfailure()) {
                finished.failure().warn("checking whether " + fields::mk(jn) +
                                        "::" + fields::mk(it.filename()) +
                                        " finished");
                res.flush();
                return finished.failure(); }
            if (finished == true) {
                res.pushtail(streamstatus::finished(sn.success(),
                                                    size.success())); }
            else {
                res.pushtail(streamstatus::partial(sn.success(),
                                                   size.success())); } }
        if (it.isfailure()) {
            it.failure().warn("listing " + fields::mk(dir));
            res.flush();
            return it.failure(); } }
    sort(res);
    maybe<streamname> newcursor(Nothing);
    if (limit != Nothing) {
        auto it(res.start());
        unsigned n;
        for (n = 0; n < limit.just() && !it.finished(); n++) it.next();
        if (!it.finished()) newcursor = it->name;
        while (!it.finished()) it.remove(); }
    auto resp(new wireproto::resp_message(rxm));
    if (newcursor != Nothing) {
        resp->addparam(proto::LISTSTREAMS::resp::cursor, newcursor.just()); }
    resp->addparam(proto::LISTSTREAMS::resp::streams, res);
    res.flush();
    return resp; }

orerror<void>
storageslave::removestream(const jobname &jn,
                           const streamname &sn) const {
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
storageslave::destroy(clientio io) {
    control_.stop();
    /* Shut down the master connection. */
    auto token(master.lock.lock());
    /* Stop anyone reconnecting. */
    master.dying = true;

    /* Stop anyone using the existing connection */
    auto mc(master.conn);
    master.conn = NULL;

    if (mc) {
        /* Start shutdown */
        mc->teardown();
        /* Wait for existing users of the connection to drop away. */
        if (master.refcount != 0) {
            subscriber sub;
            subscription ss(sub, master.idle);
            while (master.refcount != 0) {
                master.lock.unlock(&token);
                sub.wait(io);
                token = master.lock.lock(); } } }
    master.lock.unlock(&token);

    /* No more users of masterconn -> release it. */
    if (mc) mc->destroy(io);

    /* Now shut down the server */
    rpcserver::destroy(io); }

storageslave::~storageslave() {
    assert(master.conn == NULL); }

storageslave::status_t
storageslave::status() const {
    auto s(rpcserver::status());
    auto token(mux.lock());
    list<rpcconn::status_t> cl(clients.map<rpcconn::status_t>(
                                   [&token] (storageslaveconn *const &conn) {
                                       return conn->status(); }));
    mux.unlock(&token);

    auto token2(master.lock.lock());
    status_t res(s,
                 master.conn
                 ? maybe<rpcconn::status_t>(master.conn->status())
                 : Nothing,
                 cl);
    master.lock.unlock(&token2);

    cl.flush();
    return res; }

void
storageslave::status_t::addparam(
    wireproto::parameter<storageslave::status_t> tmpl,
    wireproto::tx_message &tx_msg) const {
    tx_msg.addparam(wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
                    wireproto::tx_compoundparameter()
                    .addparam(proto::storageslavestatus::server, server)
                    .addparam(
                        proto::storageslavestatus::clientconns,
                        clientconns));
    if (masterconn != Nothing) {
        tx_msg.addparam(proto::storageslavestatus::masterconn,
                        masterconn.just()); } }

storageslavestatus::~storageslavestatus() {
    clientconns.flush(); }

maybe<storageslave::status_t>
storageslave::status_t::fromcompound(const wireproto::rx_message &msg) {
    auto s(msg.getparam(proto::storageslavestatus::server));
    auto masterconn(msg.getparam(proto::storageslavestatus::masterconn));
    if (!s) return Nothing;
    list<rpcconn::status_t> clientconns;
    auto r(msg.fetch(proto::storageslavestatus::clientconns, clientconns));
    if (r.isfailure()) return Nothing;
    storageslave::status_t res(s.just(), masterconn, clientconns);
    clientconns.flush();
    return res; }
const fields::field &
fields::mk(const storageslave::status_t &o) {
    return "<storageslave: server=" + mk(o.server) +
        " master=" + mk(o.masterconn) +
        " clients=" + mk(o.clientconns) + ">"; }
