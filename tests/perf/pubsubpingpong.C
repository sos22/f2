#include "list.H"
#include "logging.H"
#include "main.H"
#include "profile.H"
#include "spark.H"
#include "string.H"
#include "timedelta.H"
#include "timestamp.H"

#include "list.tmpl"
#include "orerror.tmpl"
#include "spark.tmpl"

static unsigned long
takesample(clientio io) {
    unsigned long cntr = 0;
    bool done = false;
    unsigned phase = 0;
    publisher phase1;
    publisher phase2;
    unsigned long start;
    unsigned long end;
    {   spark<void> thr1([&] {
                assert(phase == 0);
                subscriber sub;
                subscription ss(sub, phase2);
                while (!done) {
                    cntr++;
                    phase = 1;
                    phase1.publish();
                    while (phase == 1&&!done) sub.wait(clientio::CLIENTIO); }});
        spark<void> thr2([&] {
                subscriber sub;
                subscription ss(sub, phase1);
                while (!done) {
                    while (phase != 1&&!done) sub.wait(clientio::CLIENTIO);
                    phase = 2;
                    phase2.publish(); } });
        /* Give it a second to get started. */
        (1_s).future().sleep(io);
        start = __sync_fetch_and_add(&cntr, 0);
        /* And then let it run for a bit. */
        (10_s).future().sleep(io);
        end = __sync_fetch_and_add(&cntr, 0);
        done = true;
        phase1.publish();
        phase2.publish(); }
    return end - start; }

orerror<void>
f2main(list<string> &) {
    list<unsigned long> samples;
    startprofiling();
    for (unsigned cntr = 0; cntr < 5; cntr++) {
        auto s(takesample(clientio::CLIENTIO));
        logmsg(loglevel::info, "sample " + fields::mk(s));
        samples.pushtail(s); }
    stopprofiling();
    return Success; }
