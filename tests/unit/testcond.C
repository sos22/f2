#include "clientio.H"
#include "cond.H"
#include "spark.H"
#include "test2.H"
#include "timedelta.H"

#include "spark.tmpl"
#include "test2.tmpl"

static testmodule __condtest(
    "cond",
    list<filename>::mk("cond.C", "cond.H"),
    testmodule::LineCoverage(100_pc),
    testmodule::BranchCoverage(70_pc),
    /* Not testcaseIO, even though we need a clientio token, because I
       don't want to depend on pubsub from here. */
    "wake", [] {
        mutex_t mux;
        cond_t cond(mux);
        spark<void> notify( [&mux, &cond] {
                (timestamp::now() + timedelta::milliseconds(100))
                    .sleep(clientio::CLIENTIO);
                auto token(mux.lock());
                cond.broadcast(token);
                mux.unlock(&token); });
        auto token(mux.lock());
        token = cond.wait(clientio::CLIENTIO, &token);
        mux.unlock(&token);
        notify.get(); },
    "wake2", [] {
        mutex_t mux;
        cond_t cond(mux);
        spark<void> notify( [&mux, &cond] {
                (timestamp::now() + timedelta::milliseconds(100))
                    .sleep(clientio::CLIENTIO);
                auto token(mux.lock());
                cond.broadcast(token);
                mux.unlock(&token);});
        auto token(mux.lock());
        auto r(cond.wait(clientio::CLIENTIO, &token,
                         timestamp::now() + timedelta::milliseconds(200)));
        assert(!r.timedout);
        mux.unlock(&r.token);
        notify.get(); },
    "timeout", [] {
        mutex_t mux;
        cond_t cond(mux);
        spark<void> notify( [&mux, &cond] {
                (timestamp::now() + timedelta::milliseconds(200))
                    .sleep(clientio::CLIENTIO);
                auto token(mux.lock());
                cond.broadcast(token);
                mux.unlock(&token); });
        auto token(mux.lock());
        auto r(cond.wait(clientio::CLIENTIO, &token,
                         timestamp::now() + timedelta::milliseconds(100)));
        assert(r.timedout);
        mux.unlock(&r.token);
        notify.get(); });
