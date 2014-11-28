#include "waitqueue.H"

#include "nnp.H"
#include "spark.H"
#include "string.H"
#include "test.H"
#include "timedelta.H"
#include "timestamp.H"

#include "list.tmpl"

void
tests::_waitqueue() {
    testcaseIO("waitqueue", "basics", [] (clientio io) {
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
            assert(timestamp::now() - start < timedelta::milliseconds(20)); });}
