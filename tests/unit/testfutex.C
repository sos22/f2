#include "fields.H"
#include "futex.H"
#include "logging.H"
#include "spark.H"
#include "test2.H"
#include "testassert.H"
#include "timedelta.H"
#include "util.H"

#include "spark.tmpl"
#include "test2.tmpl"
#include "testassert.tmpl"

/* This is used in the implementation of pubsub, so can't init pubsub,
 * so can't take a clientio argument. */
static clientio io = clientio::CLIENTIO;

static testmodule __testfutex(
    "futex",
    list<filename>::mk("futex.C", "futex.H"),
    testmodule::BranchCoverage(65_pc),
    "singlethread", [] {
        futex f;
        tassert(T(f.poll()) == T(0u));
        f.set(5);
        tassert(T(f.poll()) == T(5u));
        tassert(T(f.wait(io, 7)) == T(5u));
        tassert(T(f.wait(io, 7, (99999_s).future())) == T(5u));
        tassert(T(f.wait(io, 7, timestamp::now())) == T(5u));
        tassert(T(f.wait(io, 5, timestamp::now())) == T(Nothing));
        tassert(T(f.fetchadd(0)) == T(5u));
        tassert(T(f.fetchadd(1)) == T(5u));
        tassert(T(f.fetchadd(2)) == T(6u));
        tassert(T(f.poll()) == T(8u));
        auto td(timedelta::time([&] { f.wait(io, 8, (500_ms).future()); }));
        tassert(T(td) >= T(500_ms));
        tassert(T(td) < T(1_s));
        futex g(97);
        tassert(T(g.poll()) == T(97u)); },
    "basic", [] {
        racey<unsigned> cntr(0);
        {   futex a;
            futex b;
            racey<bool> finished(false);
            spark<void> thra([&] {
                    while (!finished.load()) {
                        auto r = a.wait(io, 0);
                        assert(r == 1);
                        cntr.store(cntr.load() + 1);
                        b.set(1); } });
            spark<void> thrb([&] {
                    while (!finished.load()) {
                        auto r = b.wait(io, 0);
                        assert(r == 1);
                        cntr.store(cntr.load() + 1);
                        a.set(1); } });
            (1_ms).future().sleep(io);
            assert(cntr.load() == 0);
            a.set(1);
            (3_s).future().sleep(io);
            finished.store(true);
            a.set(1);
            b.set(1); }
        assert(cntr.load() > 1000000); },
    "wakerace", [] {
        futex a;
        racey<bool> finished(false);
        spark<void> thr1([&] {
                while (!finished.load()) a.fetchadd(1); });
        spark<void> thr2([&] {
                while (!finished.load()) a.wait(io,a.poll(),(1_ms).future());});
        (1_s).future().sleep(io);
        finished.store(true); },
    "field", [] {
        futex a;
        assert(!strcmp(a.field().c_str(), "F:0"));
        a.set(12);
        assert(!strcmp(a.field().c_str(), "F:12")); });
