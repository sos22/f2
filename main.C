#include "main.H"

#include <signal.h>

#include "backtrace.H"
#include "fields.H"
#include "list.H"
#include "logging.H"
#include "string.H"
#include "thread.H"

#include "list.tmpl"
#include "orerror.tmpl"

static void
fatalsignal(int signr) {
    /* Bit of an abuse: we might already hold some memlog locks, so
       this could deadlock. We're crashing anyway, though, so it
       probably doesn't matter. */
    dumpmemlog();
    logmsg(loglevel::emergency,
           "signal " + fields::mk(signr) + " from " + backtrace().field());
    signal(signr, SIG_DFL);
    raise(signr); }

int
main(int argc, char *argv[]) {
    list<string> args;
    for (int i = 1; i < argc; i++) args.pushtail(argv[i]);
    initlogging(args);
    /* core-dumping signals should probably dump the memory log */
    signal(SIGSEGV, fatalsignal);
    signal(SIGQUIT, fatalsignal);
    signal(SIGILL, fatalsignal);
    signal(SIGABRT, fatalsignal);
    signal(SIGFPE, fatalsignal);
    signal(SIGBUS, fatalsignal);
    thread::initialthread();
    f2main(args).fatal("error from f2main");
    return 0; }

/* Basically anything which defines f2main() will need this, so put it
 * here rather than makign everyone include list.tmpl. */
template string &list<string>::idx(unsigned);
template unsigned list<string>::length() const;
