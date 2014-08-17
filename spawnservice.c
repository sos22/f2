/* fork()/exec()/wait() from a multi-threaded program causes lots of
 * issues and is just generally confusing.  Avoid them by having a
 * wrapper program which we can exec to deal with the hard parts for
 * us. */
/* Note that this isn't included in the coverage testing, because gcov
 * doesn't really work with things which always exec() or _exit() or
 * get SIGKILL'd.  Just trust that the spawn.C coverage checks are
 * sufficient. */
#define _GNU_SOURCE
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "spawnservice.h"

/* We use the self pipe trick to select() on signals.  This is the
 * write end. */
static int
selfpipewrite;

static void
sigchldhandler(int signr, siginfo_t *info, void *ignore) {
    int status;
    int e = errno;
    assert(signr == SIGCHLD);
    if (info->si_code != CLD_EXITED &&
        info->si_code != CLD_KILLED &&
        info->si_code != CLD_DUMPED) return;
    (void)ignore;
    assert(wait(&status) != -1);
    assert(write(selfpipewrite, &status, sizeof(status)) ==
           sizeof(status));
    errno = e; }

static void
execfailed(void) {
    struct message msg;
    msg.tag = msgexecfailed;
    msg.execfailed.err = errno;
    (void)write(RESPFD, &msg, sizeof(msg));
    _exit(1); }

static bool
execsucceeded(void) {
    struct message msg;
    msg.tag = msgexecgood;
    return write(RESPFD, &msg, sizeof(msg)) == sizeof(msg); }

static void
childstopped(int status) {
    struct message msg;
    msg.tag = msgchildstopped;
    msg.childstopped.status = status;
    if (write(RESPFD, &msg, sizeof(msg)) != sizeof(msg)) _exit(1); }

static void
childdied(int status) {
    childstopped(status);
    _exit(0); }

int
main(int argc, char *argv[]) {
    int p[2];
    pid_t child;
    int e;
    fd_set fds;
    struct sigaction sa;
    int selfpiperead;
    struct message msg;
    int i;
    int status;
    ssize_t r;
    bool paused;

    assert(argc >= 2);

    if (pipe2(p, O_CLOEXEC) < 0) execfailed();
    selfpipewrite = p[1];
    selfpiperead = p[0];

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigchldhandler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) < 0) execfailed();
    if (pipe2(p, O_CLOEXEC) < 0) execfailed();
    child = fork();
    if (child < 0) execfailed();
    if (child == 0) {
        /* We are the child. */
        close(p[0]);
        close(RESPFD);
        close(REQFD);
        execv(argv[1], argv + 1);
        write(p[1], &errno, sizeof(errno));
        _exit(1); }
    /* We are the parent. */
    close(p[1]);
    /* stdio should only be used from child now. */
    for (i = 0; i <= 2; i++) {
        if (i != selfpiperead && i != selfpipewrite && i != p[0]) {
            close(i); } }
    /* Wait for the child to either exec or report an error. */
    switch (read(p[0], &e, sizeof(e))) {
    case sizeof(e):
        /* execv() failed, tell parent. */
        errno = e;
        execfailed();
        abort();
    case 0:
        /* execv() succeeded, continue on our way. */
        if (execsucceeded())
            break;
        /* Fall through */
    default:
        /* Some other error communicating with child -> shoot it in
           the head and report an error. */
        kill(child, SIGKILL);
        execfailed();
        abort(); }

    paused = false;
    while (1) {
        FD_ZERO(&fds);
        FD_SET(selfpiperead, &fds);
        FD_SET(REQFD, &fds);
        if (select(selfpiperead > REQFD
                       ? selfpiperead + 1
                       : REQFD + 1,
                   &fds,
                   NULL,
                   NULL,
                   NULL) < 0) {
            if (errno == EINTR) continue;
            else _exit(3); }
        if (FD_ISSET(selfpiperead, &fds)) {
            assert(read(selfpiperead, &status, sizeof(status)) ==
                   sizeof(status));
            childdied(status); }
        if (FD_ISSET(REQFD, &fds)) {
            r = read(REQFD, &msg, sizeof(msg));
            if (r < 0) {
                /* Whoops, failed to talk to parent.  We're dead. */
                break; }
            if (r == 0) {
                /* Parent closed the pipe -> kill child and get
                 * out. */
                break; }
            if (msg.tag == msgsendsignal) {
                if (kill(child, msg.sendsignal.signr) < 0) {
                    msg.sentsignal.err = errno; }
                else {
                    msg.sentsignal.err = 0; }
                msg.tag = msgsentsignal;
                if (write(RESPFD, &msg, sizeof(msg)) != sizeof(msg)) {
                    break; } }
            else if (msg.tag == msgpause) {
                if (paused) break;
                if (kill(child, SIGSTOP) < 0) break;
                if (waitpid(child, &status, WUNTRACED) < 0) break;
                childstopped(status);
                paused = true; }
            else if (msg.tag == msgunpause) {
                if (!paused) break;
                if (kill(child, SIGCONT) < 0) break;
                paused = false; }
            else break; } }
    kill(child, SIGKILL);
    _exit(1); }
