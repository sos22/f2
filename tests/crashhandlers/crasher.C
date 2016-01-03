#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include "crashhandler.H"
#include "fields.H"
#include "list.H"
#include "main.H"
#include "mutex.H"
#include "string.H"

class chhello : public crashhandler {
public: chhello() : crashhandler(fields::mk("chhello")) {}
public: void doit() { fprintf(stderr, "\nhello crash\n"); } };

static void
crashhello(void) {
    chhello handler;
    abort(); }

static void
runforever(void) {
    class cc : public crashhandler {
    public: cc() : crashhandler(fields::mk("cc")) {}
    public: void doit() { while (true) ; } };
    cc handler;
    abort(); }

static void
crashsegv(void) {
    class cc : public crashhandler {
    public: cc() : crashhandler(fields::mk("segv")) {}
    public: void doit() { *(unsigned *)72 = 43; } };
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
    public: void doit() {
        _mux.locked([] { fprintf(stderr, "re-locked!\n"); }); } };
    cc handler(mux);
    mux.locked([] { abort(); }); }

orerror<void>
f2main(list<string> &args) {
    if (args.length() != 1) errx(1, "need a single argument");
    auto &t(args.idx(0));
    if (t == string("crashhello")) crashhello();
    else if (t == string("runforever")) runforever();
    else if (t == string("crashsegv")) crashsegv();
    else if (t == string("disablehandler")) disablehandler();
    else if (t == string("doublelock")) doublelock();
    else error::noparse.fatal("unknown crash test " + t.field());
    return Success; }
