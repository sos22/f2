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
#include "thread.tmpl"

/* The signal thread can handle any signal. */
namespace { class sigthr : public thread {
public: waitbox<void> done;
public: sigthr(const constoken &t) : thread(t) {}
public: void run(clientio io) {
    sigset_t ss;
    ::sigfillset(&ss);
    if (::sigprocmask(SIG_UNBLOCK, &ss, NULL) < 0) {
        error::from_errno().fatal("unblocking signals"); }
    done.get(io); } };
static sigthr *sigthread; }

static void
fatalsignal(int signr) {
    if (thread::me() == sigthread) {
        /* Backtraces aren't very interesting on the signal thread. */
        logmsg(loglevel::emergency, "signal " + fields::mk(signr)); }
    else {
        logmsg(loglevel::emergency,
               "signal " + fields::mk(signr) +
               " from " + backtrace().field()); }
    crashhandler::invoke();
    signal(signr, SIG_DFL);
    raise(signr); }

static void
exithandler(int code, void *) {
    if (code != 0) {
        logmsg(loglevel::emergency, "exiting with status " + fields::mk(code));
        fatalsignal(0); }
    if (sigthread != NULL) {
        /* Just to shut Valgrind up. */
        sigthread->done.set();
        sigthread->join(clientio::CLIENTIO);
        sigthread = NULL; } }

int
main(int argc, char *argv[]) {
    list<string> args;
    for (int i = 1; i < argc; i++) args.pushtail(argv[i]);
    initlogging(args);
    sigthread = thread::start<sigthr>(fields::mk("signals"));
    /* Most signals get blocked on most threads, except for ones which
     * are generated synchronously for specific instructions. */
    ::sigset_t ss;
    sigfillset(&ss);
    auto syncfatal([] (unsigned x) {
            return x == SIGSEGV ||
                x == SIGILL ||
                x == SIGBUS ||
                x == SIGFPE ||
                x == SIGABRT; });
    auto fatal([&] (unsigned x) {
            return syncfatal(x) ||
                x == SIGQUIT ||
                x == SIGALRM; });
    for (unsigned x = 1; x < 32; x++) {
        if (syncfatal(x)) sigdelset(&ss, x);
        if (fatal(x)) signal(x, fatalsignal); }
    if (::sigprocmask(SIG_BLOCK, &ss, NULL) < 0) {
        error::from_errno().fatal("sigprocmask"); }
    /* Exiting with an error is a bit like getting a fatal signal,
     * most of the time. */
    on_exit(exithandler, NULL);
    thread::initialthread();
    f2main(args).fatal("error from f2main");
    return 0; }

/* Basically anything which defines f2main() will need this, so put it
 * here rather than makign everyone include list.tmpl. */
template string &list<string>::idx(unsigned);
template unsigned list<string>::length() const;
