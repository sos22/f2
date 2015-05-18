#include "spawn.H"

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

#include "spawnservice.h"

#include "clientio.H"
#include "fields.H"
#include "test.H"
#include "timedelta.H"

#include "either.tmpl"
#include "list.tmpl"

namespace spawn {

const signalnr
signalnr::abort(SIGABRT);
const signalnr
signalnr::cont(SIGCONT);
const signalnr
signalnr::kill(SIGKILL);
const signalnr
signalnr::stop(SIGSTOP);
const signalnr
signalnr::term(SIGTERM);

const fields::field &
signalnr::field() const { return "sig" + fields::mk(snr); }

bool
signalnr::internallygenerated() const {
    return snr == SIGILL ||
        snr == SIGABRT ||
        snr == SIGBUS ||
        snr == SIGFPE ||
        snr == SIGSEGV ||
        snr == SIGPIPE ||
        snr == SIGALRM; }

program::program(const filename &f)
    : exec(f),
      args() {}

program &
program::addarg(const char *s) {
    args.pushtail(string(s));
    return *this; }

program &
program::addarg(const string &s) {
    args.pushtail(s);
    return *this; }

program &
program::addarg(string &&s) {
    args.pushtail(s);
    return *this; }

program &
program::addarg(const fields::field &f) {
    args.pushtail(string(f.c_str()));
    return *this; }

/* XXX acquiring a lock from a constructor is usually a bad sign. */
subscription::subscription(subscriber &ss, const process &p)
    : iosubscription(ss, p.fromchild.poll(POLLIN)),
      owner(&p) {
    auto t(owner->mux.lock());
    owner->nrsubs++;
    if (owner->res != Nothing) set();
    owner->mux.unlock(&t); }

void
subscription::rearm() {
    auto t(owner->mux.lock());
    if (owner->res != Nothing) set();
    owner->mux.unlock(&t); }

/* XXX not entirely happy about acquiring a lock from a destructor. */
subscription::~subscription() {
    auto t(owner->mux.lock());
    owner->nrsubs--;
    owner->mux.unlock(&t); }

orerror<process *>
process::spawn(const program &p) {
    unsigned nrargs(p.args.length());
    const char *args[nrargs + 3];
    int i(0);
    filename path(PREFIX "/spawnservice" EXESUFFIX);
    args[i++] = path.str().c_str();
    args[i++] = p.exec.str().c_str();
    for (auto it(p.args.start()); !it.finished(); it.next()) {
        args[i++] = it->c_str(); }
    args[i] = NULL;
    auto tochild(fd_t::pipe());
    if (tochild.isfailure()) return tochild.failure();
    auto fromchild(fd_t::pipe());
    if (fromchild.isfailure()) {
#ifndef COVERAGESKIP
        tochild.success().close();
        return fromchild.failure();
#endif
    }
    auto pid(::vfork());
    if (pid < 0) {
#ifndef COVERAGESKIP
        tochild.success().close();
        fromchild.success().close();
        return error::from_errno();
#endif
    }
    if (pid == 0) {
        /* gcov doesn't collect coverage information if a program ends
           at execve(), so have to exclude this bit from coverage
           checking. */
#ifndef COVERAGESKIP
        /* We are the child.  Set up FDs. */
        fromchild.success().read.close();
        tochild.success().write.close();
        int incoming(tochild.success().read.fd);
        int outgoing(fromchild.success().write.fd);
        if (incoming != REQFD) {
            if (outgoing == REQFD) {
                auto t = ::dup(outgoing);
                if (t < 0) error::from_errno().fatal("dup");
                assert(t != REQFD);
                close(outgoing);
                outgoing = t; }
            if (::dup2(incoming, REQFD) < 0) {
                error::from_errno().fatal("dup2 incoming"); }
            close(incoming); }
        if (outgoing != RESPFD) {
            if (::dup2(outgoing, RESPFD) < 0) {
                error::from_errno().fatal("dup2 outgoing"); }
            close(outgoing); }
        /* Do the exec */
        assert(execv(args[0], (char **)args) < 0);
        error::from_errno().fatal("execve");
#endif
    }
    /* We are the parent. */
    tochild.success().read.close();
    fromchild.success().write.close();
    /* Child should send us an execgood message as soon as it's
       ready. */
    struct message msg;
    auto rr(fromchild.success().read.read(
                /* spawnservice wrapper guarantees to send the message
                   quickly or to die quickly, and in either case we
                   don't need a token. */
                clientio::CLIENTIO,
                &msg,
                sizeof(msg)));
    if (rr.isfailure()) {
        /* Can't receive initial message -> exec failed. */
#ifndef COVERAGESKIP
        rr.failure().warn("execing spawnservice");
#endif
      die:
        if (::kill(pid, SIGKILL) < 0) error::from_errno().fatal("infanticide");
        if (::waitpid(pid, NULL, 0) < 0) error::from_errno().fatal("waitpid()");
        tochild.success().write.close();
        fromchild.success().read.close();
        return rr.failure(); }
    assert(rr.success() == sizeof(msg));
    if (msg.tag == message::msgexecfailed) {
        /* Report error from child. */
        rr = error::from_errno(msg.execfailed.err);
        goto die; }
    assert(msg.tag == message::msgexecgood);

    /* Child is running.  Let it go. */
    return new process(pid,
                       fromchild.success().read,
                       tochild.success().write); }

process::process(int _pid, fd_t _fromchild, fd_t _tochild)
    : pid(_pid),
      fromchild(_fromchild),
      tochild(_tochild),
      nrsubs(0),
      mux(),
      res(Nothing),
      paused(false) {}

void
process::signal(signalnr snr) {
    auto t(mux.lock());
    if (res != Nothing) {
        /* Too late */
        mux.unlock(&t);
        return; }
    struct message msg;
    msg.tag = message::msgsendsignal;
    msg.sendsignal.signr = snr.snr;
    auto sendres(tochild.write(
                     /* We never put enough in the pipe to block here,
                        so don't need a clientio token. */
                     clientio::CLIENTIO,
                     &msg,
                     sizeof(msg)));
    if (sendres.isfailure()) {
        sendres.failure().warn("sending signal message to spawn coordinator"); }
    assert(fromchild.read(
               /* Coordinator always responds quickly, so don't need a
                  token. */
               clientio::CLIENTIO,
               &msg,
               sizeof(msg))
           .fatal("reading from spawn coordinator") ==
           sizeof(msg));
    if (msg.tag == message::msgsentsignal) {
        /* We're fine. */
        assert(sendres == sizeof(msg)); }
    else {
        /* Child died before we could send the signal. */
        assert(sendres == sizeof(msg) || sendres.isfailure());
        assert(msg.tag == message::msgchildstopped);
        if (WIFEXITED(msg.childstopped.status)) {
            res = either<shutdowncode, signalnr>(
                Left(),
                shutdowncode(WEXITSTATUS(msg.childstopped.status))); }
        else {
            assert(WIFSIGNALED(msg.childstopped.status));
            res = either<shutdowncode, signalnr>(
                Right(),
                signalnr(WTERMSIG(msg.childstopped.status))); } }
    mux.unlock(&t); }

process::pausetoken
process::pause() {
    auto t(mux.lock());
    assert(!paused);
    if (res != Nothing) {
        mux.unlock(&t);
        return pausetoken(); }
    struct message msg;
    msg.tag = message::msgpause;
    auto sendres(tochild.write(
                     clientio::CLIENTIO,
                     &msg,
                     sizeof(msg)));
    if (sendres.isfailure()) {
        sendres.failure().warn("sending signal message to spawn coordinator"); }
    else assert(sendres.success() == sizeof(msg));
    assert(fromchild.read(
               clientio::CLIENTIO,
               &msg,
               sizeof(msg))
           .fatal("reading from spawn coordinator") ==
           sizeof(msg));
    assert(msg.tag == message::msgchildstopped);
    if (WIFEXITED(msg.childstopped.status)) {
        res = either<shutdowncode, signalnr>(
            Left(),
            shutdowncode(WEXITSTATUS(msg.childstopped.status))); }
    else if (WIFSIGNALED(msg.childstopped.status)) {
        res = either<shutdowncode, signalnr>(
            Right(),
            signalnr(WTERMSIG(msg.childstopped.status))); }
    else {
        assert(WIFSTOPPED(msg.childstopped.status));
        paused = true; }
    mux.unlock(&t);
    return pausetoken(); }

void
process::unpause(pausetoken) {
    auto t(mux.lock());
    if (res != Nothing) {
        mux.unlock(&t);
        return; }
    assert(paused);
    struct message msg;
    msg.tag = message::msgunpause;
    tochild.write(
        clientio::CLIENTIO,
        &msg,
        sizeof(msg))
        .fatal("unpausing child");
    paused = false;
    mux.unlock(&t); }

maybe<process::token>
process::hasdied() const {
    auto t(mux.lock());
    if (res != Nothing) {
        mux.unlock(&t);
        return token(); }
    struct message msg;
    auto r(fromchild.readpoll(&msg, sizeof(msg)));
    if (r.issuccess()) {
        assert(r == sizeof(msg));
        assert(msg.tag == message::msgchildstopped);
        if (WIFEXITED(msg.childstopped.status)) {
            res = either<shutdowncode, signalnr>(
                Left(),
                shutdowncode(WEXITSTATUS(msg.childstopped.status))); }
        else {
            assert(WIFSIGNALED(msg.childstopped.status));
            res = either<shutdowncode, signalnr>(
                Right(),
                signalnr(WTERMSIG(msg.childstopped.status))); }
        mux.unlock(&t);
        return token(); }
    mux.unlock(&t);
    return Nothing; }

either<shutdowncode, signalnr>
process::join(token) {
    assert(res != Nothing);
    assert(nrsubs == 0);
    auto rr(res.just());
    if (::kill(pid, SIGKILL) < 0) error::from_errno().fatal("kill controller");
    if (::waitpid(pid, NULL, 0) < 0) error::from_errno().fatal("waitpid()");
    tochild.close();
    fromchild.close();
    delete this;
    return rr; }

either<shutdowncode, signalnr>
process::join(clientio io) {
    auto t(hasdied());
    if (t.isjust()) return join(t.just());
    {   subscriber ss;
        subscription sub(ss, *this);
        t = hasdied();
        while (t == Nothing) {
            ss.wait(io);
            sub.rearm();
            t = hasdied(); } }
    return join(t.just()); }

void
process::kill() {
    signal(signalnr::kill);
    /* killing the child guarantees that it'll end soon, so no need
       for a clientio token. */
    join(clientio::CLIENTIO); } }
