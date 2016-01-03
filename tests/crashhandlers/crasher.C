#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include "crashhandler.H"
#include "fields.H"
#include "list.H"
#include "main.H"
#include "string.H"

static void
crashhello(void) {
    class cc : public crashhandler {
    public: cc() : crashhandler(fields::mk("cc")) {}
    public: void doit() { fprintf(stderr, "\nhello crash\n"); } };
    cc handler;
    abort(); }

orerror<void>
f2main(list<string> &args) {
    if (args.length() != 1) errx(1, "need a single argument");
    auto &t(args.idx(0));
    if (t == string("crashhello")) crashhello();
    else error::noparse.fatal("unknown crash test " + t.field());
    return Success; }
