#include "crashhandler.H"

#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "clientio.H"
#include "fields.H"
#include "logging.H"
#include "mutex.H"
#include "timedelta.H"
#include "timestamp.H"

#include "fields.tmpl"
#include "maybe.tmpl"

static crashhandler *
firstch;
static mutex_t &
handlerlock() {
    static mutex_t res;
    return res; }

static bool
crashing;

crashhandler::crashhandler(const fields::field &_name) {
    handlerlock().locked([this] {
            next = firstch;
            prev = NULL;
            if (firstch != NULL) firstch->prev = this;
            firstch = this; });
    name = ::strdup(_name.c_str()); }

crashhandler::~crashhandler() {
    handlerlock().locked([this] {
            assert((firstch == this) == (prev == NULL));
            if (next != NULL) next->prev = prev;
            if (prev != NULL) prev->next = next;
            else firstch = next; });
    free(name); }

void *
crashhandler::_allocshared(size_t sz) {
    void *res = mmap(NULL,
                     (sz + 4095) & ~4095ul,
                     PROT_READ|PROT_WRITE,
                     MAP_SHARED|MAP_ANONYMOUS,
                     -1,
                     0);
    assert(res != MAP_FAILED);
    return res; }

void
crashhandler::_releaseshared(void *ptr, size_t sz) {
    munmap(ptr, (sz + 4095) & ~4095ul); }

void
crashhandler::surrogate() {
    /* Need to proceed even if we can't get the lock... */
    auto deadline((100_ms).future());
    auto tok(handlerlock().trylock());
    while (tok == Nothing && deadline.infuture()) {
        (1_ms).future().sleep(clientio::CLIENTIO);
        tok = handlerlock().trylock(); }
    logmsg(loglevel::emergency, "invoke crash handlers, tok " + tok.field());
    /* Block SIGCHLD so that we can do timed waits. */
    sigset_t sigs;
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &sigs, NULL) < 0) {
        error::from_errno().warn("blocking sigchld"); }
    crashhandler *cursor = firstch;
    crashhandler *slowcursor = firstch;
    unsigned cntr = 0;
    while (cursor != NULL) {
        logmsg(loglevel::emergency,
               "crash handler " + fields::mk(cursor->name));
        /* Use syscall rather than fork() so that atfork handlers
         * don't run. */
        pid_t child = (pid_t)syscall(SYS_fork);
        if (child < 0) error::from_errno().warn("fork crashhandler");
        else if (child == 0) {
            /* We are the child. */
            ::crashing = true;
            /* Don't want to invoke any special sighandlers, because
             * that's mostly just confusing. */
            for (unsigned x = 0; x < 32; x++) signal(x, SIG_DFL);
            cursor->doit(crashcontext());
            syscall(SYS_exit, 0); }
        else {
            /* We are the parent. */
            timespec ts = {
                .tv_sec = 1,
                .tv_nsec = 0, };
            while (true) {
                siginfo_t si;
                int r = sigtimedwait(&sigs, &si, &ts);
                if (r < 0) {
                    if (errno == EAGAIN) {
                        logmsg(loglevel::emergency,
                               "crash handler timed out"); }
                    else error::from_errno().warn("sigtimedwait");
                    ::kill(child, SIGKILL);
                    continue; }
                assert(si.si_signo == SIGCHLD);
                assert(r == SIGCHLD);
                if (si.si_pid != child) {
                    logmsg(loglevel::error,
                           "child " + fields::mk(si.si_pid) + "died, "
                           "wanted " + fields::mk(child));
                    continue; }
                if (!WIFEXITED(si.si_status) ||
                    WEXITSTATUS(si.si_status) != 0) {
                    logmsg(loglevel::error,
                           "crash handler exited with status " +
                           fields::mk(si.si_status)); }
                break; }
            if (::waitpid(child, NULL, WNOHANG) < 0) {
                error::from_errno().warn("clearing zombie"); } }
        cursor = cursor->next;
        if (cursor == slowcursor) {
            logmsg(loglevel::emergency, "cycle in crash handler list");
            break; }
        cntr++;
        if (cntr % 2 == 0) slowcursor = slowcursor->next;
        if (cntr == 1000) {
            logmsg(loglevel::emergency, "too many crash handlers");
            break; } }
    if (tok != Nothing) handlerlock().unlock(&tok.just()); }

void
crashhandler::invoke() {
    /* If we're already in crash handler mode then do nothing. */
    if (crashhandler::crashing()) return;
    /* Fork a child and stop the parent, to get as clean a report as
     * possible. */
    pid_t spid = (pid_t)syscall(SYS_fork);
    if (spid < 0) error::from_errno().warn("fork CH surrogate");
    else if (spid > 0) ::waitpid(spid, NULL, 0);
    else {
        pid_t ppid = (pid_t)syscall(SYS_getppid);
        ::kill(ppid, SIGSTOP);
        surrogate();
        ::kill(ppid, SIGCONT);
        _exit(0); } }

bool
crashhandler::crashing() { return ::crashing; }
