#include "clientio.H"
#include "cond.H"
#include "logging.H"
#include "spark.H"
#include "test.H"
#include "test2.H"
#include "timedelta.H"

#include "spark.tmpl"
#include "test2.tmpl"

static testmodule __condtest(
    "cond",
    list<filename>::mk("cond.C", "cond.H"),
    testmodule::LineCoverage(100_pc),
    testmodule::BranchCoverage(75_pc),
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
        notify.get(); },
    testmodule::TestFlags::valgrind(), "valgrindtimeout", [] {
        mutex_t mux;
        cond_t cond(mux);
        auto t(mux.lock());
        auto d((100_ms).future());
        cond.wait(clientio::CLIENTIO, &t, d);
        assert(timestamp::now() > d);
        mux.unlock(&t);
        assert(timestamp::now() < d + 500_ms); }
#if TESTING
    ,
    "longtimeout", [] {
        unsigned nr = 0;
        tests::eventwaiter< ::loglevel> waiter(
            tests::logmsg,
            [&nr] (loglevel level) { if (level >= loglevel::error) nr++; });
        mutex_t mux;
        cond_t cond(mux);
        auto t(mux.lock());
        cond.wait(clientio::CLIENTIO, &t, timestamp::now());
        assert(nr == 0);
        spark<void> kick([&] {
                (1_s).future().sleep(clientio::CLIENTIO);
                mux.locked([&] (mutex_t::token tok) { cond.broadcast(tok);});});
        cond.wait(clientio::CLIENTIO, &t, ((86400_s) * 10).future());
        assert(nr == 1);
        mux.unlock(&t); }
#endif
);
