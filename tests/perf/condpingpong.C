/* Simple test case which tests how fast we can do pingpong on cond
 * variables, so that we have a reference point for the pubsub
 * implementation. */
#include <pthread.h>

#include "clientio.H"
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
    pthread_mutex_t mux;
    pthread_mutex_init(&mux, NULL);
    pthread_cond_t cond;
    pthread_cond_init(&cond, NULL);
    unsigned long cntr = 0;
    bool done = false;
    unsigned phase = 0;
    unsigned long start;
    unsigned long end;
    {   spark<void> thr1([&] {
                assert(phase == 0);
                pthread_mutex_lock(&mux);
                while (!done) {
                    cntr++;
                    phase = 1;
                    pthread_cond_signal(&cond);
                    while (phase == 1 && !done) pthread_cond_wait(&cond, &mux);}
                pthread_mutex_unlock(&mux); });
        spark<void> thr2([&] {
                pthread_mutex_lock(&mux);
                while (!done) {
                    while (phase != 1 && !done) pthread_cond_wait(&cond, &mux);
                    phase = 2;
                    pthread_cond_signal(&cond); }
                pthread_mutex_unlock(&mux); });
        /* Give it a second to get started. */
        (1_s).future().sleep(io);
        start = __sync_fetch_and_add(&cntr, 0);
        /* And then let it run for a bit. */
        (10_s).future().sleep(io);
        end = __sync_fetch_and_add(&cntr, 0);
        done = true;
        pthread_cond_broadcast(&cond); }
    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&mux);
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
