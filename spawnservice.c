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
#include <sys/prctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "spawnservice.h"

static int
reqfd;

static int
respfd;

/* We use the self pipe trick to select() on signals.  This is the
 * write end. */
static int
selfpipewrite;

static double
now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec * 1e-9; }

static void
sigchldhandler(int signr, siginfo_t *info, void *ignore) {
    int status;
    int e = errno;
    write(2, "sighandler\n", 11);
    assert(signr == SIGCHLD);
    if (info->si_code != CLD_EXITED &&
        info->si_code != CLD_KILLED &&
        info->si_code != CLD_DUMPED) {
        char buf[10];
        buf[4] = '\n';
        buf[3] = "0123456789abcdef"[(info->si_code >> 24) % 16];
        buf[2] = "0123456789abcdef"[(info->si_code >> 16) % 16];
        buf[1] = "0123456789abcdef"[(info->si_code >> 8) % 16];
        buf[0] = "0123456789abcdef"[info->si_code % 16];
        write(2, buf, 5);
        return; }
    (void)ignore;
    assert(wait(&status) != -1);
    assert(write(selfpipewrite, &status, sizeof(status)) ==
           sizeof(status));
    write(2, "wrote pipe\n", 11);
    errno = e; }

static void
execfailed(void) {
    struct message msg;
    msg.tag = msgexecfailed;
    msg.execfailed.err = errno;
    (void)write(respfd, &msg, sizeof(msg));
    _exit(1); }

static bool
execsucceeded(void) {
    struct message msg;
    msg.tag = msgexecgood;
    return write(respfd, &msg, sizeof(msg)) == sizeof(msg); }

static void
childstopped(int status) {
    struct message msg;
    msg.tag = msgchildstopped;
    msg.childstopped.status = status;
    if (write(respfd, &msg, sizeof(msg)) != sizeof(msg)) _exit(1); }

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

    assert(argc >= 4);

    respfd = atoi(argv[1]);
    reqfd = atoi(argv[2]);

    prctl(PR_SET_PDEATHSIG, SIGKILL);

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
        close(respfd);
        close(reqfd);
        /* Don't outlive our parent. */
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        execv(argv[3], argv + 3);
        write(p[1], &errno, sizeof(errno));
        _exit(1); }
    /* We are the parent. */
    close(p[1]);
    fprintf(stderr, "%f in spawn parent\n", now());
    /* Close the FDs we don't need. */
    for (i = 3; i <= 1000; i++) {
        if (i != selfpiperead &&
            i != selfpipewrite &&
            i != p[0] &&
            i != respfd &&
            i != reqfd) {
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
    fprintf(stderr, "%f child done exec\n", now());

    paused = false;
    while (1) {
        FD_ZERO(&fds);
        FD_SET(selfpiperead, &fds);
        FD_SET(reqfd, &fds);
        if (select(selfpiperead > reqfd
                       ? selfpiperead + 1
                       : reqfd + 1,
                   &fds,
                   NULL,
                   NULL,
                   NULL) < 0) {
            if (errno == EINTR) continue;
            else _exit(3); }
        if (FD_ISSET(selfpiperead, &fds)) {
            fprintf(stderr, "%f self pipe readable\n", now());
            assert(read(selfpiperead, &status, sizeof(status)) ==
                   sizeof(status));
            fprintf(stderr, "%f child has died %o\n", now(), status);
            childdied(status); }
        if (FD_ISSET(reqfd, &fds)) {
            fprintf(stderr, "%f reqfd readable\n", now());
            r = read(reqfd, &msg, sizeof(msg));
            if (r < 0) {
                /* Whoops, failed to talk to parent.  We're dead. */
                break; }
            if (r == 0) {
                /* Parent closed the pipe -> kill child and get
                 * out. */
                break; }
            if (msg.tag == msgsendsignal) {
                fprintf(stderr,
                        "%f send signal %d\n",
                        now(),
                        msg.sendsignal.signr);
                if (kill(child, msg.sendsignal.signr) < 0) {
                    msg.sentsignal.err = errno; }
                else {
                    msg.sentsignal.err = 0; }
                msg.tag = msgsentsignal;
                if (write(respfd, &msg, sizeof(msg)) != sizeof(msg)) {
                    break; } }
            else if (msg.tag == msgpause) {
                fprintf(stderr, "%f pause child\n", now());
                if (paused) break;
                if (kill(child, SIGSTOP) < 0) break;
                if (waitpid(child, &status, WUNTRACED) < 0) break;
                childstopped(status);
                paused = true; }
            else if (msg.tag == msgunpause) {
                fprintf(stderr, "%f unpause child\n", now());
                if (!paused) break;
                if (kill(child, SIGCONT) < 0) break;
                paused = false; }
            else break; } }
    fprintf(stderr, "%f all done\n", now());
    kill(child, SIGKILL);
    _exit(1); }
