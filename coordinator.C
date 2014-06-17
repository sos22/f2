#include "coordinator.H"

#include "controlserver.H"
#include "fields.H"
#include "logging.H"
#include "mastersecret.H"
#include "peername.H"
#include "proto.H"
#include "registrationsecret.H"
#include "rpcconn.H"

class coordinatorimpl : public coordinator {
private: mastersecret ms;
private: registrationsecret rs;

    /* Control server interface */
private: class statusinterface : public rpcinterface {
    public:  statusinterface() : rpcinterface(proto::COORDINATORSTATUS::tag) {}
    private: maybe<error> message(const wireproto::rx_message &,
                                  rpcconn &,
                                  buffer &);
    };
private: statusinterface statusiface;
private: rpcregistration *controlregistration;

    /* Coordination server interface */
private: class hellointerface : public rpcinterface {
    private: coordinatorimpl *const owner;
    public:  hellointerface(coordinatorimpl *_owner)
        : rpcinterface(proto::HELLO::tag),
          owner(_owner) {}
    private: maybe<error> message(const wireproto::rx_message &,
                                  rpcconn &,
                                  buffer &);
    };
private: hellointerface helloiface;
private: rpcregistration *coordregistration;

public:  coordinatorimpl(const mastersecret &ms,
                         const registrationsecret &rs,
                         controlserver *cs);
private: void destroy();
};

maybe<error>
coordinatorimpl::statusinterface::message(const wireproto::rx_message &,
                                          rpcconn &,
                                          buffer &) {
    return error::unimplemented; }

maybe<error>
coordinatorimpl::hellointerface::message(const wireproto::rx_message &msg,
                                         rpcconn &conn,
                                         buffer &) {
    auto from(conn.peer());
    auto version(msg.getparam(proto::HELLO::req::version));
    auto nonce(msg.getparam(proto::HELLO::req::nonce));
    auto slavename(msg.getparam(proto::HELLO::req::slavename));
    auto digest(msg.getparam(proto::HELLO::req::digest));
    if (!version || !nonce || !slavename || !digest) {
        return error::missingparameter; }
    logmsg(loglevel::verbose,
           "HELLO version " + fields::mk(version) +
           "nonce " + fields::mk(nonce) +
           "slavename " + fields::mk(slavename) +
           "digest " + fields::mk(digest));
    if (version.just() != 1) {
        return error::badversion; }
    if (!owner->ms.noncevalid(nonce.just(), slavename.just())) {
        logmsg(loglevel::notice,
               "HELLO with invalid nonce from " + fields::mk(from));
        return error::authenticationfailed; }
    if (!slavename.just().samehost(from)) {
        logmsg(loglevel::notice,
               "HELLO with bad host (" + fields::mk(slavename.just()) +
               ", expected host " + fields::mk(from) +")");
        return error::authenticationfailed; }
    if (digest.just() != ::digest("B" +
                                  fields::mk(nonce.just()) +
                                  fields::mk(owner->rs))) {
        logmsg(loglevel::notice,
               "HELLO with invalid digest from " + fields::mk(from));
        return error::authenticationfailed; }
    logmsg(loglevel::notice, "Valid HELLO from " + fields::mk(from));
    return error::unimplemented; }

coordinatorimpl::coordinatorimpl(
    const mastersecret &_ms,
    const registrationsecret &_rs,
    controlserver *cs)
    : ms(_ms),
      rs(_rs),
      statusiface(),
      controlregistration(cs->registeriface(statusiface)),
      helloiface(this),
      coordregistration(
          cs->registeriface(multiregistration()
                            .add(helloiface))) {}

void
coordinatorimpl::destroy() {
    stop();
    coordregistration->destroy();
    controlregistration->destroy();
    rpcserver::destroy(); }

orerror<coordinator *>
coordinator::build(
    const mastersecret &ms,
    const registrationsecret &rs,
    const peername &listenon,
    controlserver *cs) {
    auto res(new coordinatorimpl(ms, rs, cs));
    auto r(res->start(listenon, fields::mk("coordinator")));
    if (r.isjust()) {
        delete res;
        return r.just(); }
    return res; }
