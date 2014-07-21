#include "storageslave.H"

#include "beaconclient.H"
#include "filename.H"
#include "parsers.H"
#include "jobname.H"
#include "streamname.H"
#include "tcpsocket.H"

#include "filename.tmpl"
#include "list.tmpl"
#include "parsers.tmpl"
#include "rpcconn.tmpl"
#include "wireproto.tmpl"

wireproto_wrapper_type(storageslave::status_t);

class storageslaveconn : public rpcconn {
    friend class rpcconn;
private: storageslave *const owner;
private: storageslaveconn(socket_t &_socket,
                          const rpcconnauth &_auth,
                          const peername &_peer,
                          storageslave *_owner);
private: messageresult message(const wireproto::rx_message &);
private: void endconn(clientio);
};

storageslaveconn::storageslaveconn(
    socket_t &_socket,
    const rpcconnauth &__auth,
    const peername &_peer,
    storageslave *_owner)
    : rpcconn(_socket, __auth, _peer),
      owner(_owner) {
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
        if (res.isjust()) return res.just();
        else return new wireproto::resp_message(rxm);
    } else if (rxm.tag() == proto::APPEND::tag) {
        auto job(rxm.getparam(proto::APPEND::req::job));
        auto stream(rxm.getparam(proto::APPEND::req::stream));
        auto bytes(rxm.getparam(proto::APPEND::req::bytes));
        if (!job || !stream || !bytes) return error::missingparameter;
        if (bytes.just().empty()) return error::invalidparameter;
        auto res(owner->append(job.just(), stream.just(), bytes.just()));
        if (res.isjust()) return res.just();
        else return new wireproto::resp_message(rxm);
    } else if (rxm.tag() == proto::FINISH::tag) {
        auto job(rxm.getparam(proto::FINISH::req::job));
        auto stream(rxm.getparam(proto::FINISH::req::stream));
        if (!job || !stream) return error::missingparameter;
        auto res(owner->finish(job.just(), stream.just()));
        if (res.isjust()) return res.just();
        else return new wireproto::resp_message(rxm);
    } else {
        return rpcconn::message(rxm); } }

void
storageslaveconn::endconn(clientio) {
    auto token(owner->mux.lock());
    for (auto it(owner->clients.start()); true; it.next()) {
        if (*it == this) {
            it.remove();
            break; } }
    owner->mux.unlock(&token); }

void
storageslave::statusiface::getstatus(wireproto::tx_message *msg) const {
    msg->addparam(proto::STATUS::resp::storageslave, owner->status()); }

orerror<storageslave *>
storageslave::build(clientio io,
                    const registrationsecret &rs,
                    const filename &dir,
                    controlserver *cs) {
    auto name(
        (dir + "slavename").parse(parsers::slavename())
        .fatal("parsing slave name from " + fields::mk(dir)));
    auto br(beaconclient(rs));
    if (br.isfailure()) return br.failure();
    auto res(new storageslave(
                 br.success().secret,
                 dir,
                 cs));
    auto mc(rpcconn::connectmaster<storageslaveconn>(
                io,
                br.success(),
                name,
                res));
    if (mc.isfailure()) {
        delete res;
        return mc.failure(); }
    res->masterconn = mc.success();
    auto r(res->listen(peername::tcpany()));
    if (r.isjust()) {
        res->masterconn = NULL;
        delete res;
        mc.success()->destroy(io);
        return r.just(); }
    res->status_.start();
    return res; }

storageslave::storageslave(const registrationsecret &_rs,
                           const filename &_pool,
                           controlserver *cs)
    : status_(this, cs),
      rs(_rs),
      masterconn(NULL),
      clients(),
      pool(_pool),
      mux() {}

orerror<rpcconn *>
storageslave::accept(socket_t s) {
    return rpcconn::fromsocket<storageslaveconn>(
        s,
        rpcconnauth::mksendhelloslavea(rs),
        this); }

/* XXX this can sometimes leave stuff behind after a partial
 * failure. */
maybe<error>
storageslave::createempty(const jobname &t, const streamname &sn) const {
    logmsg(loglevel::debug,
           "create empty output " + fields::mk(t) + " " + fields::mk(sn));
    filename jobdir(pool + t.asfilename());
    {   auto r(jobdir.mkdir());
        if (r.isjust() && r.just() != error::already) return r.just(); }
    filename streamdir(jobdir + sn.asfilename());
    {   auto r(streamdir.mkdir());
        if (r.isjust() && r.just() != error::already) return r.just(); }
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
        if (r != Nothing && r != error::already) return r.just(); }
    /* Create the new content file. */
    {   auto r(content.createfile());
        if (r == Nothing || r == error::already) return Nothing; }
    {   auto r(content.unlink());
        if (r != Nothing) return r.just(); }
    return content.createfile(); }

maybe<error>
storageslave::append(
    const jobname &jn,
    const streamname &sn,
    buffer &b) const {
    filename dirname(pool + jn.asfilename() + sn.asfilename());
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
    return Nothing; }

maybe<error>
storageslave::finish(
    const jobname &jn,
    const streamname &sn) const {
    filename dirname(pool + jn.asfilename() + sn.asfilename());
    {   auto content((dirname + "content").exists());
        if (content.isfailure()) return content.failure();
        else if (content == false) return error::notfound; }
    filename finished(dirname + "finished");
    auto exists(finished.exists());
    if (exists.isfailure()) return exists.failure();
    else if (exists == true) return error::already;
    return finished.createfile(); }

void
storageslave::destroy(clientio io) {
    status_.stop();
    /* Stop the master connection now, but don't release it until
       we've finished tearing down our clients.  Not clear whether the
       two-step is actually necessary, but it's a lot easier to think
       about than the synchronisation around a one-step. */
    masterconn->teardown();
    subscriber sub;
    subscription ss(sub, masterconn->deathpublisher());
    while (!masterconn->hasdied()) sub.wait();
    rpcserver::destroy(io); }

storageslave::~storageslave() {
    if (masterconn != NULL) {
        auto dt(masterconn->hasdied());
        /* Wait for master death before calling rpcconn::destroy(), so
           it should still be dead when we get here. */
        assert(dt != Nothing);
        masterconn->destroy(dt.just()); } }

storageslave::status_t
storageslave::status() const {
    assert(masterconn);
    auto token(mux.lock());
    list<rpcconn::status_t> cl(clients.map<rpcconn::status_t>(
                                   [&token] (storageslaveconn *const &conn) {
                                       return conn->status(token); }));
    status_t res(masterconn->status(token), cl);
    mux.unlock(&token);
    cl.flush();
    return res; }

void
storageslave::status_t::addparam(
    wireproto::parameter<storageslave::status_t> tmpl,
    wireproto::tx_message &tx_msg) const {
    tx_msg.addparam(wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
                    wireproto::tx_compoundparameter()
                    .addparam(proto::storageslavestatus::masterconn, masterconn)
                    .addparam(
                        proto::storageslavestatus::clientconns,
                        clientconns)); }
maybe<storageslave::status_t>
storageslave::status_t::fromcompound(const wireproto::rx_message &msg) {
    auto masterconn(msg.getparam(proto::storageslavestatus::masterconn));
    if (!masterconn) return Nothing;
    list<rpcconn::status_t> clientconns;
    auto r(msg.fetch(proto::storageslavestatus::clientconns, clientconns));
    if (r.isjust()) return Nothing;
    storageslave::status_t res(masterconn.just(), clientconns);
    clientconns.flush();
    return res; }
const fields::field &
fields::mk(const storageslave::status_t &o) {
    return "<storageslave: master=" + mk(o.masterconn) +
        " clients=" + mk(o.clientconns) + ">"; }
