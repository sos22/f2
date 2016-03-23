#include "spark.H"
#include "test2.H"
#include "testassert.H"
#include "timedelta.H"
#include "util.H"

#include "test2.tmpl"
#include "testassert.tmpl"

static struct testmodule __testutil(
    "util",
    list<filename>::mk("util.C", "util.H"),
    testmodule::LineCoverage(60_pc),
    testmodule::BranchCoverage(14_pc),
    "singlethreadracey", [] {
        racey<unsigned> x(5);
        assert(x.load() == 5);
        x.store(12);
        assert(x.load() == 12);
        assert(x.loadacquire() == 12);
        x.storerelease(19);
        assert(x.loadacquire() == 19);
        assert(!strcmp(x.field().c_str(), "19"));
        assert(x.fetchadd(2) == 19);
        assert(x.load() == 21); },
    "twovars", [] (clientio) {
        racey<unsigned> a(0);
        racey<unsigned> b(0);
        racey<unsigned> seqlock(0);
        racey<bool> finished(false);
        spark<void> thr([&] {
                while (!finished.load()) {
                    if (running_on_valgrind()) sched_yield();
                    seqlock.store(seqlock.load() + 1);
                    if (running_on_valgrind()) sched_yield();
                    a.store(a.load() + 1);
                    if (running_on_valgrind()) sched_yield();
                    b.store(b.load() + 1);
                    if (running_on_valgrind()) sched_yield();
                    seqlock.store(seqlock.load() + 1); } });
        auto deadline((1_s).future());
        unsigned cntrs[] = {0,0,0};
        while (deadline.infuture()) {
            if (running_on_valgrind()) sched_yield();
            auto s1 = seqlock.load();
            if (running_on_valgrind()) sched_yield();
            auto bb = b.load();
            if (running_on_valgrind()) sched_yield();
            auto aa = a.load();
            if (running_on_valgrind()) sched_yield();
            auto s2 = seqlock.load();
            if (s1 == s2) {
                if (s1 & 1) {
                    tassert(T(aa) == T(bb) || T(aa) == T(bb + 1));
                    cntrs[0]++; }
                else {
                    tassert(T(aa) == T(bb));
                    cntrs[1]++; } }
            else {
                tassert(T(aa) >= T(bb));
                cntrs[2]++; } }
        finished.store(true);
        tassert(T(cntrs[0]) > T(0u));
        tassert(T(cntrs[1]) > T(0u));
        tassert(T(cntrs[2]) > T(0u)); },
    "cmpswap", [] (clientio io) {
        racey<unsigned> x(0);
        assert(x.compareswap(5, 10) == 0);
        assert(x.load() == 0);
        assert(x.compareswap(0, 10) == 0);
        assert(x.load() == 10);
        racey<bool> lock(false);
        racey<bool> finished(false);
        x.store(0);
        spark<void> thr1([&] {
                while (!finished.load()) {
                    if (lock.compareswap(0, 1) == 0) {
                        assert(x.load() == 0);
                        x.store(1);
                        x.store(0);
                        lock.store(0); } } });
        spark<void> thr2([&] {
                while (!finished.load()) {
                    if (lock.compareswap(0, 2) == 0) {
                        assert(x.load() == 0);
                        x.store(2);
                        x.store(0);
                        lock.store(0); } } });
        (1_s).future().sleep(io);
        finished.store(true); },
    "increment", [] (clientio io) {
        racey<unsigned> a(0);
        racey<unsigned> b(0);
        unsigned aa(0);
        unsigned bb(0);
        {   racey<bool> finished(false);
            spark<void> thra([&] {
                    while (!finished.load()) {
                        a.fetchadd(100);
                        aa += 100;
                        b.fetchadd(100);
                        bb += 100; } });
            spark<void> thrb([&] {
                    while (!finished.load()) {
                        b.fetchadd(1);
                        bb++;
                        a.fetchadd(1);
                        aa++; } });
            (1_s).future().sleep(io);
            finished.store(true); }
        tassert(T(a.load()) == T(b.load()));
        tassert(T(aa) != T(bb) || T(running_on_valgrind())); });
