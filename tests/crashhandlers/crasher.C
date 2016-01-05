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

class chhello : public crashhandler {
public: chhello() : crashhandler(fields::mk("chhello")) {}
public: void doit(crashcontext) override {
    fprintf(stderr, "\nhello crash\n"); } };

static void
crashhello(void) {
    chhello handler;
    abort(); }

static void
runforever(void) {
    class cc : public crashhandler {
    public: cc() : crashhandler(fields::mk("cc")) {}
    public: void doit(crashcontext) override { while (true) ; } };
    cc handler;
    abort(); }

static void
crashsegv(void) {
    class cc : public crashhandler {
    public: cc() : crashhandler(fields::mk("segv")) {}
    public: void doit(crashcontext) override { *(unsigned *)72 = 43; } };
    cc h1;
    chhello handler;
    cc h2;
    abort(); }

static void
disablehandler(void) {
    {   chhello h; }
    abort(); }

static void
doublelock(void) {
    mutex_t mux;
    class cc : public crashhandler {
    public: mutex_t &_mux;
    public: cc(mutex_t &__mux)
        : crashhandler(fields::mk("cc")),
          _mux(__mux) {}
    public: void doit(crashcontext) override {
        _mux.locked([] { fprintf(stderr, "re-locked!\n"); }); } };
    cc handler(mux);
    mux.locked([] { abort(); }); }

static void
dumplog(void) {
    logmsg(loglevel::debug, "this is a debug message");
    abort(); }

static void
changesinvisible(void) {
    unsigned z = 0;
    class cc : public crashhandler {
    public: unsigned &_z;
    public: cc(unsigned &__z)
        : crashhandler(fields::mk("cc")),
          _z(__z) {}
    public: void doit(crashcontext) override {
        assert(_z == 0);
        _z++;
        fprintf(stderr, "zzz %d\n", _z); } };
    cc h1(z);
    cc h2(z);
    abort(); }

static void
sharedstate(void) {
    unsigned &z(crashhandler::allocshared<unsigned>());
    class cc : public crashhandler {
    public: unsigned &_z;
    public: cc(unsigned &__z)
        : crashhandler(fields::mk("cc")),
          _z(__z) {}
    public: void doit(crashcontext) override {
        _z++;
        fprintf(stderr, "zzz %d\n", _z); } };
    cc h1(z);
    cc h2(z);
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
    class cc : public crashhandler {
    public: unsigned &_cntr;
    public: cc(unsigned &__cntr)
        : crashhandler(fields::mk("cc")),
          _cntr(__cntr) {}
    public: void doit(crashcontext) override {
        fprintf(stderr, "\nZZZ %d\n", _cntr); } };
    cc h1(cntr);
    cc h2(cntr);
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
