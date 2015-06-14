/* Various RPC services useful for writing test cases. */
#ifndef TESTSERVICES_H__
#define TESTSERVICES_H__

#include "rpcservice2.H"
#include "spark.H"

class slowservice : public rpcservice2 {
public: list<spark<void> > outstanding;
public: slowservice(const rpcservice2::constoken &t)
    : rpcservice2(t, interfacetype::test) {}
public: orerror<void> called(
    clientio,
    deserialise1 &ds,
    interfacetype,
    nnp<incompletecall> ic,
    onconnectionthread) final {
    timedelta delay(ds);
    unsigned key(ds);
    if (ds.isfailure()) return ds.failure();
    auto deadline(timestamp::now() + delay);
    outstanding.append([deadline, key, ic] {
            {   subscriber sub;
                subscription ss(sub, ic->abandoned().pub());
                while (deadline.infuture() && !ic->abandoned().ready()) {
                    sub.wait(clientio::CLIENTIO, deadline); } }
            ic->complete(
                [key] (serialise1 &s, mutex_t::token) { s.push(key); },
                acquirestxlock(clientio::CLIENTIO)); });
    return Success; } };

#endif
