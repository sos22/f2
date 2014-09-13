#include "coordinator.H"

#include "fields.H"
#include "proto.H"
#include "logging.H"
#include "spark.H"
#include "test.H"

#include "list.tmpl"
#include "rpcconn.tmpl"
#include "rpcserver.tmpl"
#include "spark.tmpl"
#include "wireproto.tmpl"

#include "fieldfinal.H"

wireproto_wrapper_type(coordinatorstatus);

wireproto_wrapper_type(coordinatorconnstatus);
void
coordinatorconnstatus::addparam(
    wireproto::parameter<coordinatorconnstatus> tmpl,
    wireproto::tx_message &txm) const {
    wireproto::tx_compoundparameter p;
    p.addparam(proto::coordinatorconnstatus::conn, connstatus);
    if (name.isjust()) {
        p.addparam(proto::coordinatorconnstatus::slave, name.just()); }
    txm.addparam(wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
                 p); }
maybe<coordinatorconnstatus>
coordinatorconnstatus::fromcompound(const wireproto::rx_message &rxm) {
    auto c(rxm.getparam(proto::coordinatorconnstatus::conn));
    if (!c) return Nothing;
    return coordinatorconnstatus(
        c.just(),
        rxm.getparam(proto::coordinatorconnstatus::slave)); }

class coordinatorconn : public rpcconn {
    friend class pausedthread<coordinatorconn>;
public:  coordinator *const owner;
public:  coordinatorconn(const rpcconn::rpcconntoken &token,
                         coordinator *_owner);
private: void endconn(clientio);

public:  typedef coordinatorconnstatus status_t;
public:  status_t status() {
    return status_t( rpcconn::status(), slavename() ); }
};

coordinatorconn::coordinatorconn(const rpcconn::rpcconntoken &tok,
                                 coordinator *_owner)
    : rpcconn(tok),
      owner(_owner) {
    auto token(owner->mux.lock());
    owner->connections.pushtail(this);
    owner->mux.unlock(&token); }

void
coordinatorconn::endconn(clientio) {
    auto token(owner->mux.lock());
    bool found = false;
    for (auto it(owner->connections.start()); !it.finished(); it.next()) {
        if (*it == this) {
            found = true;
            it.remove();
            break; } }
    assert(found);
    owner->mux.unlock(&token); }

void
coordinator::controlinterface::getstatus(wireproto::tx_message *msg) const {
    msg->addparam(proto::STATUS::resp::coordinator, owner->status()); }

void
coordinator::controlinterface::getlistening(
    wireproto::resp_message *msg) const {
    msg->addparam(proto::LISTENING::resp::coordinator, owner->localname()); }

coordinator::status_t
coordinator::status() const {
    list<coordinatorconnstatus> c;
    auto token(mux.lock());
    for (auto it(connections.start()); !it.finished(); it.next()) {
        c.pushtail((*it)->status()); }
    mux.unlock(&token);
    coordinator::status_t res(c);
    c.flush();
    return res; }

coordinator::coordinator(
    constoken token,
    listenfd fd,
    const mastersecret &_ms,
    const registrationsecret &_rs,
    controlserver *cs,
    const rpcconnconfig &_config)
    : rpcserver(token, fd),
      ms(_ms),
      rs(_rs),
      connconfig(_config),
      controliface(this, cs) {
    controliface.start(); }

orerror<rpcconn *>
coordinator::accept(socket_t s) {
    return rpcconn::fromsocket<coordinatorconn>(
        s,
        rpcconnauth::mkwaithello(ms, rs, &slaveconnected),
        connconfig,
        this); }

coordinator::iterator::iterator(const coordinator *owner,
                                actortype type)
    : content(),
      /* Will be replaced with something correct later. */
      cursor(content.start()) {
    /* Acquiring a lock from a constructor.  Probably okay: it's a
     * small leaf lock. */
    auto token(owner->mux.lock());
    for (auto it(owner->connections.start()); !it.finished(); it.next()) {
        if ((*it)->type() == type) {
            auto sn((*it)->slavename());
            if (sn != Nothing) content.pushtail(sn.just()); } }
    owner->mux.unlock(&token);
    cursor.~iter();
    new (&cursor) list<slavename>::iter(content.start()); }

slavename
coordinator::iterator::get() const {
    /* Caller has to make sure we're not finish()ed. */
    return *cursor; }

void
coordinator::iterator::next() {
    cursor.next(); }

bool
coordinator::iterator::finished() const {
    return cursor.finished(); }

coordinator::iterator::~iterator() {
    content.flush(); }

coordinator::iterator
coordinator::start(actortype t) const {
    return iterator(this, t); }

maybe<pair<rpcconn *, rpcconn::reftoken> >
coordinator::get(const slavename &_name) const {
    auto token(mux.lock());
    for (auto it(connections.start());
         !it.finished();
         it.next()) {
        if ((*it)->slavename() == _name) {
            auto res(pair<rpcconn *, rpcconn::reftoken>(
                         *it, (*it)->reference()));
            mux.unlock(&token);
            return res; } }
    mux.unlock(&token);
    return Nothing; }

void
coordinator::destroy(clientio io) {
    controliface.stop();
    rpcserver::destroy(io); }

orerror<coordinator *>
coordinator::build(
    const mastersecret &ms,
    const registrationsecret &rs,
    const peername &listenon,
    controlserver *cs,
    const rpcconnconfig &config) {
    return rpcserver::listen<coordinator>(listenon, ms, rs, cs, config)
        .map<coordinator *>([] (pausedrpcserver<coordinator> c) {
                return c.go(); }); }

const fields::field &
fields::mk(const coordinatorconn::status_t &o) {
    return "<coordinatorconn " + mk(o.connstatus) +
        " name:" + mk(o.name) + ">"; }

void
coordinator::status_t::addparam(
    wireproto::parameter<coordinator::status_t> tmpl,
    wireproto::tx_message &tx_msg) const {
    tx_msg.addparam(wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
                    wireproto::tx_compoundparameter()
                    .addparam(proto::coordinatorstatus::conns, conns)); }
maybe<coordinator::status_t>
coordinator::status_t::fromcompound(const wireproto::rx_message &msg) {
    list<coordinatorconn::status_t> conns;
    auto r(msg.fetch(proto::coordinatorstatus::conns, conns));
    if (r.isfailure()) return Nothing;
    coordinator::status_t res(conns);
    conns.flush();
    return res; }
const fields::field &
fields::mk(const coordinator::status_t &o) {
    const field *res = &fields::mk("<conns:{");
    bool first = true;
    for (auto it(o.conns.start()); !it.finished(); it.next()) {
        if (!first) res = &(*res + ",");
        res = &(*res + mk(*it)); }
    return *res + "}>"; }

void
tests::_coordinator() {
    testcaseCS(
        "coordinator",
        "oneclient",
        [] (controlserver *cs, clientio io) {
            initlogging("T");
            auto ms(mastersecret::mk());
            registrationsecret rs((quickcheck()));
            peername listenon(peername::loopback(peername::port(quickcheck())));
            auto config(rpcconnconfig::dflt);
            /* Try to speed disconnect detection up a bit. */
            config.pinginterval = timedelta::milliseconds(100);
            auto coord(coordinator::build(
                           ms,
                           rs,
                           listenon,
                           cs,
                           config)
                       .fatal("building test coordinator"));
            /* List should start empty */
            {   auto i(coord->start(actortype::test));
                assert(i.finished()); }
            subscriber sub;
            subscription someonearrived(sub, coord->slaveconnected);

            /* Connect a client */
            waitbox<void> connected;
            waitbox<void> authenticate;
            spark<rpcconn *> client(
                [&authenticate, &connected, io, &listenon, &ms, &rs] {
                    auto conn(rpcconn::connectnoauth<rpcconn>(
                                  io,
                                  ::slavename("<test>"),
                                  actortype::master,
                                  listenon,
                                  rpcconnconfig::dflt)
                              .fatal("connecting to coordinator"));
                    connected.set();
                    auto ln(conn->localname());
                    auto nonce(ms.nonce(ln));
                    authenticate.get(io);
                    conn->call(
                        io,
                        wireproto::req_message(proto::HELLO::tag,
                                               conn->allocsequencenr())
                        .addparam(proto::HELLO::req::version, 1u)
                        .addparam(proto::HELLO::req::nonce, nonce)
                        .addparam(proto::HELLO::req::peername,
                                  conn->localname())
                        .addparam(proto::HELLO::req::slavename,
                                  ::slavename("<testconn>"))
                        .addparam(proto::HELLO::req::type,
                                  actortype::test)
                        .addparam(proto::HELLO::req::digest,
                                  digest("B" +
                                         fields::mk(nonce) +
                                         fields::mk(rs))));
                    return conn; });
            connected.get(io);
            /* Connected but not authenticated -> shouldn't be in list */
            {   auto i(coord->start(actortype::test));
                assert(i.finished()); }
            /* publisher shouldn't have been set */
            assert(sub.poll() == NULL);
            /* Let it connect. */
            authenticate.set();
            auto conn(client.get());
            /* Should have set the publisher */
            assert(sub.poll() == &someonearrived);
            /* New conn should be in list */
            {   auto i(coord->start(actortype::test));
                assert(!i.finished());
                assert(i.get() == ::slavename("<testconn>"));
                i.next();
                assert(i.finished()); }
            /* Disconnect */
            conn->destroy(io);
            /* Conn should drop out of list */
            (timestamp::now() +
             config.pinginterval +
             timedelta::milliseconds(50))
                .sleep(io);
            {   auto i(coord->start(actortype::test));
                assert(i.finished()); }
            /* Done */
            someonearrived.detach();
            coord->destroy(io); }); }
