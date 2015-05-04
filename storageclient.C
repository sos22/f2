#include "storageclient.H"

#include "clientio.H"
#include "nnp.H"
#include "orerror.H"
#include "proto2.H"
#include "serialise.H"
#include "storage.H"
#include "util.H"

template <typename t> typename t::resT
popasync(clientio io, t &cl) {
    auto tok(cl.finished());
    if (tok == Nothing) {
        subscriber ss;
        subscription sub(ss, cl.pub());
        tok = cl.finished();
        while (tok == Nothing) {
            ss.wait(io);
            tok = cl.finished(); } }
    return cl.pop(tok.just()); }

class storageclient::impl {
public: class storageclient api;
public: connpool &cp;
public: const agentname an;
public: impl(connpool &, const agentname &);
public: void destroy(); };

storageclient::impl::impl(connpool &_cp, const agentname &_an)
    : cp(_cp), an(_an) {}

void
storageclient::impl::destroy() { delete this; }

class storageclient::impl &
storageclient::impl() { return *containerof(this, class impl, api); }

const class storageclient::impl &
storageclient::impl() const { return *containerof(this, class impl, api); }

class storageclient::asyncconnect::impl {
public: storageclient::asyncconnect api;
public: connpool &cp;
public: const agentname an;
public: connpool::asynccall &cl;
public: impl(connpool &_cp, const agentname &_an)
    : cp(_cp),
      an(_an),
      cl(*cp.call(an,
                  interfacetype::storage,
                  Nothing,
                  [] (serialise1 &s, connpool::connlock) {
                      s.push(proto::storage::tag::ping); })) {}
public: maybe<storageclient::asyncconnect::token> finished() const {
    auto r(cl.finished());
    if (r == Nothing) return Nothing;
    else return token(r.just()); }
public: storageclient::asyncconnect::resT pop(
    storageclient::asyncconnect::token t) {
    auto r(cl.pop(t.inner));
    if (r.isfailure()) {
        logmsg(
            loglevel::failure,
            "failed to connect to " + an.field() + ": " + r.failure().field());
        delete this;
        return r.failure(); }
    else {
        logmsg(
            loglevel::debug,
            "connected to " + an.field());
        auto res(new class storageclient::impl(cp, an));
        delete this;
        return success(_nnp(res->api)); } } };

class storageclient::asyncconnect::impl &
storageclient::asyncconnect::impl() {
    return *containerof(this, class impl, api); }

const class storageclient::asyncconnect::impl &
storageclient::asyncconnect::impl() const {
    return *containerof(this, class impl, api); }

maybe<storageclient::asyncconnect::token>
storageclient::asyncconnect::finished() const { return impl().finished(); }

const publisher &
storageclient::asyncconnect::pub() const { return impl().cl.pub(); }

storageclient::asyncconnect::resT
storageclient::asyncconnect::pop(token t) { return impl().pop(t); }

void
storageclient::asyncconnect::abort() {
    impl().cl.abort();
    delete &impl(); }

storageclient::asyncconnect &
storageclient::connect(connpool &cp, const agentname &an) {
    return (new class asyncconnect::impl(cp, an))->api; }

orerror<nnp<storageclient> >
storageclient::connect(
    clientio io,
    connpool &cp,
    const agentname &an) {
    return popasync(io, connect(cp, an)); }

void
storageclient::destroy() { impl().destroy(); }
