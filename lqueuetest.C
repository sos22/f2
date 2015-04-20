#include "lqueue.H"

#include "buffer.H"
#include "logging.H"
#include "spark.H"
#include "test.H"

#include "spark.tmpl"
#include "test.tmpl"

void
tests::_lqueue() {
    testcaseIO("lqueue", "basics", [] (clientio) {
            evtsrc<int> src(1.0_s);
            src.push(5);
            evtdest<int> dest1(src);
            assert(dest1.pop() == Nothing);
            src.push(6);
            assert(dest1.pop() == 6);
            assert(dest1.pop() == Nothing);
            evtdest<int> dest2(src);
            assert(dest1.pop() == Nothing);
            assert(dest2.pop() == Nothing);
            src.push(7);
            src.push(8);
            src.push(9);
            assert(dest1.pop() == 7);
            assert(dest1.pop() == 8);
            assert(dest2.pop() == 7);
            assert(dest1.pop() == 9);
            assert(dest1.pop() == Nothing);
            assert(dest2.pop() == 8);
            src.push(10);
            assert(dest2.pop() == 9);
            assert(dest2.pop() == 10);
            assert(dest2.pop() == Nothing);
            assert(dest1.pop() == 10);
            assert(dest1.pop() == Nothing); });
    testcaseIO("lqueue", "pub", [] (clientio io) {
            evtsrc<int> src(1_s);
            evtdest<int> dest1(src);
            {   spark<void> sucker([&] { assert(dest1.pop(io) == 11); });
                (1_ms).future().sleep(io);
                src.push(11);
                assert(timedelta::time([&]{sucker.get();}) < 100_ms); }
            evtdest<int> dest2(src);
            {   spark<void> sucker([&] {
                        assert(dest1.pop(io) == 12);
                        assert(dest2.pop(io) == 12); });
                src.push(12);
                assert(timedelta::time([&]{sucker.get();}) < 100_ms); } });
    testcaseIO("lqueue", "gc", [] (clientio) {
            /* Don't try to allocate tens of gigs of memory if we're
             * sucking out as fast as events get generated. */
            unsigned char kilobyte[1024];
            memset(kilobyte, 'a', sizeof(kilobyte));
            ::buffer hundredK;
            for (unsigned x = 0; x < 100; x++) {
                hundredK.queue(kilobyte, sizeof(kilobyte)); }
            evtsrc< ::buffer> src(1_s);
            evtdest< ::buffer> dest(src);
            for (unsigned x = 0; x < 100000; x++) {
                src.push(hundredK);
                assert(dest.pop().just().contenteq(hundredK)); } });
#if TESTING
    testcaseIO("lqueue", "lag", [] (clientio io) {
            /* Stuff sitting in the queue for too long should generate
             * a log warning. */
            evtsrc<int> src(10_ms);
            evtdest<int> dest(src);
            src.push(1);
            bool generatedwarning = false;
            eventwaiter<loglevel> hook(
                tests::logmsg,
                [&generatedwarning] (loglevel a) {
                    if (a >= loglevel::failure) generatedwarning = true; });
            (15_ms).future().sleep(io);
            src.push(2);
            assert(generatedwarning);
            assert(dest.pop() == 1);
            assert(dest.pop() == 2);
            generatedwarning = false;
            src.push(3);
            assert(dest.pop() == 3);
            assert(generatedwarning == false); });
#endif
    testcaseIO("lqueue", "destructflush", [] (clientio) {
            evtsrc<int> src(1_s);
            evtdest<int> dest1(src);
            {   evtdest<int> dest2(src);
                src.push(1);
                src.push(2); }
            assert(dest1.pop() == 1); });
    testcaseIO("lqueue", "concurrency", [] (clientio io) {
            /* One thread generates events continuously, the other one
             * constantly subscribes and unsubscribes. */
            evtsrc<unsigned long> src(60_s);
            bool stop = false;
            spark<void> producer([&src, &stop] {
                    unsigned long cntr = 1;
                    while (!stop) src.push(cntr++); });
            spark<void> consumer([&src, &stop] {
                    unsigned long last = 0;
                    while (!stop) {
                        evtdest<unsigned long> dst(src);
                        auto r(dst.pop(clientio::CLIENTIO));
                        assert(r > last);
                        last = r; } });
            (2_s).future().sleep(io);
            stop = true;
            consumer.get();
            producer.get(); });
}
