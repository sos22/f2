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
#include <sys/wait.h>
#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "spawnservice.h"

static int
reqfd;

static int
respfd;

/* We need a SIGCHLD handler so that it breaks us out of ppoll() */
static void
sigchldhandler(int signr, siginfo_t *info, void *ignore) {
    (void)signr;
    (void)info;
    (void)ignore; }

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
    pid_t c;
    int e;
    struct sigaction sa;
    struct message msg;
    int status;
    ssize_t r;
    bool paused;
    sigset_t sigs;
    int fl;
    DIR *fddir;
    struct dirent *de;
    long fd;
    char *eof;

    assert(argc >= 4);

    respfd = atoi(argv[1]);
    reqfd = atoi(argv[2]);

    sigemptyset(&sigs);
    sigprocmask(SIG_SETMASK, NULL, &sigs);
    sigaddset(&sigs, SIGCHLD);
    sigprocmask(SIG_BLOCK, &sigs, NULL);

    prctl(PR_SET_PDEATHSIG, SIGKILL);

    fl = fcntl(reqfd, F_GETFL);
    fl |= O_NONBLOCK;
    fcntl(reqfd, F_SETFL, fl);

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
    /* Close the FDs we don't need. */
    fddir = opendir("/proc/self/fd");
    if (fddir == NULL) err(1, "open /proc/self/fd");
    while (true) {
        errno = 0;
        de = readdir(fddir);
        if (de == NULL) {
            if (errno == 0) break;
            else err(1, "readdir /proc/self/fd"); }
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue; }
        fd = strtoll(de->d_name, &eof, 10);
        if (errno != 0) err(1, "parsing fd number %s", de->d_name);
        if (*eof != '\0') errx(1, "noise at end of fd %s", de->d_name);
        if (fd < 0 || fd > 65536) errx(1, "bad fd %ld", fd);
        if (fd != p[0] && fd != respfd && fd != reqfd && fd != dirfd(fddir)) {
            close((int)fd); } }
    closedir(fddir);
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
        struct pollfd pfd;
        memset(&pfd, 0, sizeof(pfd));
        pfd.fd = reqfd;
        pfd.events = POLLIN;
        sigdelset(&sigs, SIGCHLD);
        r = ppoll(&pfd, 1, NULL, &sigs);
        if (r < 0) {
            if (errno != EINTR) break;
            c = waitpid(-1, &status, WNOHANG);
            if (c == -1) break;
            if (c == 0) continue;
            childdied(status); }
        assert(pfd.revents & POLLIN);
        r = read(reqfd, &msg, sizeof(msg));
        if (r < 0) {
            if (errno == EWOULDBLOCK) continue;
            break; }
        if (r == 0) {
            /* Parent closed the pipe -> kill child and get out. */
            break; }
        if (msg.tag == msgsendsignal) {
            if (kill(child, msg.sendsignal.signr) < 0) {
                msg.sentsignal.err = errno; }
            else {
                msg.sentsignal.err = 0; }
            msg.tag = msgsentsignal;
            if (write(respfd, &msg, sizeof(msg)) != sizeof(msg)) {
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
        else break; }
    kill(child, SIGKILL);
    _exit(1); }
