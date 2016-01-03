#include "main.H"

#include <signal.h>
#include <stdlib.h>

#include "backtrace.H"
#include "crashhandler.H"
#include "fields.H"
#include "list.H"
#include "logging.H"
#include "string.H"
#include "thread.H"

#include "list.tmpl"
#include "orerror.tmpl"

static void
fatalsignal(int signr) {
    logmsg(loglevel::emergency,
           "signal " + fields::mk(signr) + " from " + backtrace().field());
    crashhandler::invoke();
    signal(signr, SIG_DFL);
    raise(signr); }

static void
exithandler(int code, void *) {
    if (code != 0) {
        logmsg(loglevel::emergency, "exiting with status " + fields::mk(code));
        fatalsignal(0); } }
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
    /* Similarly any time we exit with an error. */
    on_exit(exithandler, NULL);
    thread::initialthread();
    f2main(args).fatal("error from f2main");
    return 0; }

/* Basically anything which defines f2main() will need this, so put it
 * here rather than makign everyone include list.tmpl. */
template string &list<string>::idx(unsigned);
template unsigned list<string>::length() const;
