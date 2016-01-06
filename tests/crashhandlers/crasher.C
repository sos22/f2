#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include "crashhandler.H"
#include "fields.H"
#include "list.H"
#include "logging.H"
#include "main.H"
#include "mutex.H"
#include "spark.H"
#include "string.H"

#include "crashhandler.tmpl"
#include "spark.tmpl"

static void
crashhello(void) {
    crashhandler ch(
        fields::mk("chhello"),
        [] (crashcontext) { fprintf(stderr, "\nhello crash\n"); });
    abort(); }

static void
runforever(void) {
    crashhandler ch(
        fields::mk("cc"),
        [] (crashcontext) { while (true) ; });
    abort(); }

static void
crashsegv(void) {
    crashhandler s1(
        fields::mk("s1"),
        [] (crashcontext) { *(unsigned *)72=43; });
    crashhandler ch(
        fields::mk("chhello"),
        [] (crashcontext) { fprintf(stderr, "\nhello crash\n"); });
    crashhandler s2(
        fields::mk("s2"),
        [] (crashcontext) { *(unsigned *)78=43; });
    abort(); }

static void
disablehandler(void) {
    crashhandler(
        fields::mk("chhello"),
        [] (crashcontext) { fprintf(stderr, "\nhello crash\n"); });
    abort(); }

static void
doublelock(void) {
    mutex_t mux;
    crashhandler handler(
        fields::mk("cc"),
        [&] (crashcontext) {
            mux.locked([] { fprintf(stderr, "re-locked!\n"); }); });
    mux.locked([] { abort(); }); }

static void
dumplog(void) {
    logmsg(loglevel::debug, "this is a debug message");
    abort(); }

static void
changesinvisible(void) {
    unsigned z = 0;
    auto inner([&] (crashcontext) {
            assert(z == 0);
            z++;
            fprintf(stderr, "zzz %d\n", z); });
    crashhandler h1(fields::mk("h1"), inner);
    crashhandler h2(fields::mk("h2"), inner);
    abort(); }

static void
sharedstate(void) {
    auto &z(crashhandler::allocshared<unsigned>());
    auto inner([&] (crashcontext) {
            z++;
            fprintf(stderr, "zzz %d\n", z); });
    crashhandler h1(fields::mk("h1"), inner);
    crashhandler h2(fields::mk("h2"), inner);
    abort(); }

static void
releaseshared(void) {
    struct bigstruct { char content[1 << 20]; };
    for (unsigned x = 0; x < 20000; x++) {
        auto &t(crashhandler::allocshared<bigstruct>());
        /* Force allocation by writing to every page. */
        for (unsigned y = 0; y < sizeof(t.content); y += 4096) {
            t.content[y] = 7; }
        crashhandler::releaseshared(t); }
    /* This one doesn't actually crash; we're just checking that
     * releasing shared state actually releases memory. */ }

static void
stopotherthreads(void) {
    auto &cntr(crashhandler::allocshared<unsigned>());
    cntr = 0;
    spark<void> countup([&] { cntr++; });
    auto inner([&] (crashcontext) {
            fprintf(stderr, "\nZZZ %d\n", cntr); });
    crashhandler h1(fields::mk("h1"), inner);
    crashhandler h2(fields::mk("h2"), inner);
    abort(); }

orerror<void>
f2main(list<string> &args) {
    if (args.length() != 1) errx(1, "need a single argument");
    auto &t(args.idx(0));
    if (t == string("crashhello")) crashhello();
    else if (t == string("runforever")) runforever();
    else if (t == string("crashsegv")) crashsegv();
    else if (t == string("disablehandler")) disablehandler();
    else if (t == string("doublelock")) doublelock();
    else if (t == string("dumplog")) dumplog();
    else if (t == string("changesinvisible")) changesinvisible();
    else if (t == string("sharedstate")) sharedstate();
    else if (t == string("releaseshared")) releaseshared();
    else if (t == string("stopotherthreads")) stopotherthreads();
    else error::noparse.fatal("unknown crash test " + t.field());
    return Success; }
