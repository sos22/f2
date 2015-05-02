#include "spark.H"
#include "test2.H"
#include "timedelta.H"
#include "waitqueue.H"

#include "spark.tmpl"
#include "test2.tmpl"

static testmodule __waitqueuetest(
    "waitqueue",
    list<filename>::mk("waitqueue.H"),
    testmodule::LineCoverage(90_pc),
    testmodule::BranchCoverage(60_pc),
    "basics", [] (clientio io) {
        waitqueue<string> wq;
        assert(wq.pop() == Nothing);
        wq.push(string("Hello"));
        wq.append("Goodbye");
        assert(wq.pop() == string("Hello"));
        assert(wq.pop() == "Goodbye");
        assert(wq.pop() == Nothing);
        auto s(spark<void>([io, &wq] { assert(wq.pop(io) == "foo"); }));
        auto start(timestamp::now());
        wq.append("foo");
        s.get();
        assert(timestamp::now() - start < timedelta::milliseconds(20)); });
