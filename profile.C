/* Noddy little profiler, because I can't figure out how to drive
 * gprof.  Each thread has a private (closed) hash table to store
 * local events and there's then a worker thread which periodically
 * flushes them out to a global table. */
#include "profile.H"

#include <err.h>
#include <signal.h>
#include <signal.h>
#include <time.h>
#include <ucontext.h>

#ifndef sigev_notify_thread_id
#define sigev_notify_thread_id   _sigev_un._tid
#endif

#include "clientio.H"
#include "cond.H"
#include "list.H"
#include "map.H"
#include "mutex.H"
#include "timedelta.H"
#include "timestamp.H"
#include "util.H"

#include "list.tmpl"
#include "map.tmpl"
#include "mutex.tmpl"
#include "timedelta.tmpl"

/* How often to sample RIP */
#define SAMPLEINTERVAL (1_ms)
/* How often to merge the per-thread samples into the global table. */
#define HARVESTINTERVAL (1_s)

struct sample {
    unsigned cntr;
    racey<unsigned long> rip;
    sample() : cntr(0), rip(0) {}
};

struct threadstate {
    unsigned long discards;
    sample samples[1021]; };

namespace _profiler {
    /* Shouldn't really do __thread from a signal handler.  We'll
     * probably get away with it if it's all nicely set up from each
     * thread before the signal fires, though.  Good enough for a
     * debug aide, anyway. */
    static __thread threadstate *firstlevel;
    static __thread timer_t timerhandle;
    static __thread bool threnabled;
    /* Bits needed to communicate with the monitor thread. */
    static mutex_t lock;
    static cond_t cond(lock);
    static bool shutdown;
    static bool enabled;
    static list<threadstate> threads;
    /* Don't use thread_t, because we always want to exclude this
     * thread from profiling. */
    static pthread_t monitor;
    static map<unsigned long, unsigned long> secondlevel;
    static unsigned long discards; }

unsigned long foocntr;

static void
sigusr1handler(int /* sigalrm */,
               siginfo_t * /*info */,
               void *_ucontext) {
    auto ucontext = (const ucontext_t *)_ucontext;
    unsigned long rip = ucontext->uc_mcontext.gregs[REG_RIP];
    auto ts = _profiler::firstlevel;
    foocntr++;
    /* Try to find a suitable hash slot. */
    for (unsigned x = 0; x < 10; x++) {
        auto idx = (rip + x * 16381) % ARRAYSIZE(ts->samples);
        auto r(ts->samples[idx].rip.load());
        if (r == 0) {
            ts->samples[idx].rip.store(rip);
            r = rip; }
        if (r == rip) {
            /* XXX there is a race here where samples can get lost if
             * we're racing with the monitor thread clearing the
             * table.  Oh well. */
            /* (The cast is there to shut up a stupid compiler error,
             * because apparently the g++ developers don't think
             * packed structures are ever useful) */
            atomicinc(*(unsigned *)&ts->samples[idx].cntr);
            return; } }
    /* Didn't find a slot.  Give up and drop the sample. */
    atomicinc(ts->discards); }

static void
integratethread(threadstate *ts, mutex_t::token /* profile lock */) {
    using namespace _profiler;
    for (unsigned x(0); x < ARRAYSIZE(ts->samples); x++) {
        /* This is obviously racey, but the worst possible outcome is
         * one or two samples being misaccounted every couple of
         * seconds, which should be tolerable. */
        auto rip(ts->samples[x].rip.load());
        if (rip == 0) continue;
        auto cntr(atomicswap(*(unsigned *)&ts->samples[x].cntr, 0u));
        ts->samples[x].rip.store(0);
        if (ts->samples[x].cntr != 0) {
            cntr += atomicswap(*(unsigned *)&ts->samples[x].cntr, 0u); }
        /* <<< end racey bit >>> */
        auto m(secondlevel.getptr(rip));
        if (m == NULL) secondlevel.set(rip, cntr);
        else *m += cntr; }
    discards += atomicswap(ts->discards, 0ul); }

static void *
monitorthread(void * /* ctxt */) {
    using namespace _profiler;
    auto tok(lock.lock());
    auto nextsample(HARVESTINTERVAL.future());
    while (!shutdown) {
        auto r(cond.wait(clientio::CLIENTIO, &tok, nextsample));
        tok = r.token;
        if (!r.timedout) continue;
        for (auto it(threads.start()); !it.finished(); it.next()) {
            integratethread(&*it, tok); } }
    lock.unlock(&tok);
    auto f(fopen("profile.raw", "w"));
    if (!f) err(1, "fopen(\"profile.raw\")");
    fprintf(f, "discard %ld\n", discards);
    auto m(fopen("/proc/self/maps", "r"));
    if (!m) err(1, "fopen(\"/proc/self/maps\")");
    char *lineptr(NULL);
    size_t lineptrsize(0);
    while (true) {
        auto s = getline(&lineptr, &lineptrsize, m);
        if (s < 0) {
            if (!feof(m)) err(1, "reading /proc/self/maps");
            break; }
        lineptrsize = s;
        unsigned long start;
        unsigned long end;
        char r;
        char w;
        char x;
        char p;
        unsigned long offset;
        unsigned dev0;
        unsigned dev1;
        unsigned long ino;
        char *path;
        int nr = sscanf(lineptr,
                        "%lx-%lx %c%c%c%c %lx %x:%x %lx %ms",
                        &start, &end, &r, &w, &x, &p, &offset, &dev0, &dev1,
                        &ino, &path);
        if (nr < 11 || r != 'r' || x != 'x') continue;
        fprintf(f, "map %lx %lx %lx %s\n", start, end, offset, path);
        free(path); }
    fclose(m);
    free(lineptr);
    for (auto it(secondlevel.start()); !it.finished(); it.next()) {
        fprintf(f, "sample %ld 0x%lx\n", it.value(), it.key()); }
    if (fclose(f) == EOF) err(1, "fclose(\"profile.raw\")");
    return NULL; }

void
startprofiling(void) {
    struct sigaction sa;
    sa.sa_sigaction = sigusr1handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    if (sigaction(SIGUSR1, &sa, NULL) < 0) err(1, "sigaction()");
    auto e = pthread_create(&_profiler::monitor, NULL, monitorthread, NULL);
    if (e) {
        errno = e;
        err(1, "pthread_create(monitorthread)"); }
    _profiler::enabled = true;
    profilenewthread(); }

void
stopprofiling(void) {
    profileendthread();
    if (!_profiler::enabled) return;
    _profiler::lock.locked([] (mutex_t::token tok) {
            _profiler::shutdown = true;
            _profiler::cond.broadcast(tok); });
    pthread_join(_profiler::monitor, NULL);
    _profiler::enabled = false; }

void
profilenewthread(void) {
    if (!_profiler::enabled) return;
    auto l(_profiler::lock.locked<threadstate *>([] {
                auto ll(&_profiler::threads.append());
                memset(ll, 0, sizeof(*ll));
                return ll; }));
    _profiler::firstlevel = l;
    sigevent evt;
    evt.sigev_notify = SIGEV_THREAD_ID;
    evt.sigev_signo = SIGUSR1;
    evt.sigev_notify_thread_id = tid::me().os();
    if (timer_create(CLOCK_REALTIME, &evt, &_profiler::timerhandle)) {
        err(1, "timer_create()"); }
    itimerspec spec;
    spec.it_interval = SAMPLEINTERVAL.astimespec();
    spec.it_value = spec.it_interval;
    if (timer_settime(_profiler::timerhandle, 0, &spec, NULL)) {
        err(1, "timer_settime()"); }
    sigset_t sigusr1;
    sigemptyset(&sigusr1);
    sigaddset(&sigusr1, SIGUSR1);
    if (sigprocmask(SIG_UNBLOCK, &sigusr1, NULL)) {
        err(1, "sigprocmask(SIG_UNBLOCK, SIGUSR1)"); }
    _profiler::threnabled = true; }

void
profileendthread(void) {
    if (!_profiler::threnabled) return;
    /* Stop any more signals being delivered. */
    if (timer_delete(_profiler::timerhandle) < 0) {
        err(1, "timer_delete()"); }
    /* Make sure there aren't any queued up anywhere.  There shouldn't
     * be anyway; this is just paranoia. */
    sigset_t sigusr1;
    sigemptyset(&sigusr1);
    sigaddset(&sigusr1, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &sigusr1, NULL) < 0) {
        err(1, "sigprocmask(SIG_BLOCK, SIGUSR1)"); }
    _profiler::lock.locked([] (mutex_t::token tok) {
            integratethread(_profiler::firstlevel, tok);
            _profiler::threads.drop(_profiler::firstlevel); }); }
