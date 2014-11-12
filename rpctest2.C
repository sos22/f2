#include "test.H"

#include <arpa/inet.h>

#include "buffer.H"
#include "logging.H"
#include "nnp.H"
#include "quickcheck.H"
#include "rpcclient2.H"
#include "rpcservice2.H"
#include "serialise.H"
#include "spark.H"
#include "string.H"
#include "timedelta.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "rpcclient2.tmpl"
#include "rpcservice2.tmpl"
#include "spark.tmpl"
#include "test.tmpl"
#include "timedelta.tmpl"

namespace tests {

class echoservice : public rpcservice2 {
private: unsigned cntr;
public:  echoservice(const rpcservice2::constoken &t)
    : rpcservice2(t, interfacetype::test),
      cntr(73) {}
public: orerror<void> called(
    clientio,
    onconnectionthread oct,
    deserialise1 &ds,
    interfacetype,
    nnp<incompletecall> ic) final {
    string msg(ds);
    if (ds.isfailure()) return ds.failure();
    if (cntr == 75) {
        cntr++;
        ic->fail(error::toolate, oct); }
    else {
        ic->complete(
            [&msg, this] (serialise1 &s,
                          mutex_t::token,
                          onconnectionthread) {
                msg.serialise(s);
                s.push(cntr++); },
            oct); }
    return Success; } };

class abandonservice : public rpcservice2 {
public: maybe<spark<void> > worker;
public: waitbox<void> &abandoned;
public: abandonservice(const rpcservice2::constoken &t,
                       waitbox<void> &_abandoned)
    : rpcservice2(t, interfacetype::test),
      worker(Nothing),
      abandoned(_abandoned) {}
public: orerror<void> called(
    clientio,
    onconnectionthread,
    deserialise1 &,
    interfacetype,
    nnp<incompletecall> ic) final {
    worker.mkjust([this, ic] () {
            ic->abandoned().get(clientio::CLIENTIO);
            abandoned.set();
            ic->fail(error::toolate); });
    return Success; } };

class slowservice : public rpcservice2 {
public: list<spark<void> > outstanding;
public: slowservice(const rpcservice2::constoken &t)
    : rpcservice2(t, interfacetype::test) {}
public: orerror<void> called(
    clientio,
    onconnectionthread,
    deserialise1 &ds,
    interfacetype,
    nnp<incompletecall> ic) final {
    timedelta delay(ds);
    unsigned key(ds);
    if (ds.isfailure()) return ds.failure();
    auto deadline(timestamp::now() + delay);
    outstanding.append([deadline, key, ic] {
            {   subscriber sub;
                subscription ss(sub, ic->abandoned().pub);
                while (deadline.infuture() && !ic->abandoned().ready()) {
                    sub.wait(clientio::CLIENTIO, deadline); } }
            ic->complete([key] (serialise1 &s, mutex_t::token) {
                    s.push(key); }); });
    return Success; } };

void
rpctest2() {
    testcaseIO("rpctest2", "doublelisten", [] (clientio io) {
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
            auto srv(rpcservice2::listen<echoservice>(
                         io,
                         cn,
                         sn,
                         peername::all(peername::port::any))
                     .fatal("starting echo service"));
            auto port(srv->port());
            assert(
                rpcservice2::listen<echoservice>(io, cn, sn,peername::all(port))
                == error::from_errno(EADDRINUSE));
            srv->destroy(io);
            srv = rpcservice2::listen<echoservice>(io,cn,sn,peername::all(port))
                .fatal("restarting echo service");
            srv->destroy(io); } );
    testcaseIO("rpctest2", "badconnect", [] (clientio io) {
            /* Make up a peername which probably respond quickly. */
            struct sockaddr_in sa;
            memset(&sa, 0, sizeof(sa));
            sa.sin_family = AF_INET;
            sa.sin_addr.s_addr = 0x08090a0b;
            sa.sin_port = 12345;
            assert(rpcclient2::connect(
                       io,
                       peername((struct sockaddr *)&sa, sizeof(sa)),
                       timestamp::now()) == error::timeout);
            assert(rpcclient2::connect(
                       io,
                       peername::loopback(peername::port(1)))
                   == error::from_errno(ECONNREFUSED)); });
    testcaseIO("rpctest2", "abortconnect", [] (clientio io) {
            /* Arrange to abort after doing the connect syscall but
             * before we do the HELLO */
            quickcheck q;
            clustername cn(q);
            slavename sn(q);
            waitbox<void> doneconnect;
            tests::hook<void> h(
                rpcclient2::doneconnectsyscall,
                [&doneconnect] () {
                    doneconnect.set(); });
            auto srv(rpcservice2::listen<echoservice>(
                         io,
                         cn,
                         sn,
                         peername::loopback(peername::port::any))
                     .fatal("starting echo service"));
            auto conn(rpcclient2::connect(peername::loopback(srv->port())));
            doneconnect.get(io);
            conn->abort();
            srv->destroy(io); });
} }
