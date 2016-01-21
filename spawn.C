#include "spawn.H"

#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>

#include <valgrind/valgrind.h>

#include "spawnservice.h"

#include "clientio.H"
#include "fields.H"
#include "fuzzsched.H"
#include "logging.H"
#include "test.H"
#include "timedelta.H"

#include "either.tmpl"
#include "list.tmpl"
#include "map.tmpl"
#include "mutex.tmpl"
#include "orerror.tmpl"

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
      args(),
      fds() {}

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

program &
program::addfd(fd_t inparent, int inchild) {
    for (auto it(fds.start()); !it.finished(); it.next()) {
        assert(it.value() != inparent); }
    fds.set(inchild, inparent);
    return *this; }

const fields::field &
program::field() const {
    auto acc(&("{" + exec.str().field()));
    for (auto it(args.start()); !it.finished(); it.next()) {
        acc= &(*acc + " " + it->field()); }
    list<pair<int, int> > redirects;
    for (auto it(fds.start()); !it.finished(); it.next()) {
        redirects.append(it.key(), it.value().fd); }
    ::sort(redirects);
    for (auto it(redirects.start()); !it.finished(); it.next()) {
        acc = &(*acc + " " +
                fields::mk(it->first()) + "->" + fields::mk(it->second())); }
    return *acc + "}"; }

orerror<either<shutdowncode, signalnr> >
program::run(clientio io) const {
    auto p(process::spawn(*this));
    if (p.isfailure()) return p.failure();
    else return p.success()->join(io); }

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
    const char *args[nrargs + 5];
    int i(0);
    filename path(PREFIX "/spawnservice" EXESUFFIX);
    args[i++] = path.str().c_str();
    i += 2; /* Leave a gap for the control FDs */
    args[i++] = p.exec.str().c_str();
    for (auto it(p.args.start()); !it.finished(); it.next()) {
        args[i++] = it->c_str(); }
    args[i] = NULL;
    auto tochild(fd_t::pipe());
    if (tochild.isfailure()) return tochild.failure();
    auto fromchild(fd_t::pipe());
    if (fromchild.isfailure()) {
        tochild.success().close();
        return fromchild.failure(); }
    logmsg(loglevel::debug, "starting spawnservice");
    auto pid(::vfork());
    if (pid < 0) {
        auto e(error::from_errno());
        tochild.success().close();
        fromchild.success().close();
        return e; }
    if (pid == 0) {
        fromchild.success().read.close();
        tochild.success().write.close();
        if (::setsid() < 0) error::from_errno().fatal("setsid");
        auto childread = tochild.success().read;
        auto childwrite = fromchild.success().write;
        /* We are the child. */
        auto fds(p.fds);
        /* Shuffle FDs into the right places. */
        for (auto it(fds.start()); !it.finished(); it.next()) {
            if (it.key() == it.value().fd) continue;
            /* Have to move it->value() to it->key(). */
            /* Move existing it->key() out of the way first, if we're
             * eventually going to need it. */
            maybe<fd_t> _movedto(Nothing);
            auto movedto([&] {
                    if (_movedto == Nothing) {
                        int r = ::dup(it.key());
                        if (r < 0) {
                            error::from_errno().fatal(
                                "dupe " + fields::mk(it.key())); }
                        _movedto = fd_t(r); }
                    return _movedto.just(); });
            for (auto it2(fds.start()); !it2.finished(); it2.next()) {
                if (it2.key() == it.key()) continue;
                if (it2.value().fd == it.key()) it2.value() = movedto(); }
            if (childread.fd == it.key()) childread = movedto();
            if (childwrite.fd == it.key()) childwrite = movedto();
            int r = ::dup2(it.value().fd, it.key());
            if (r < 0) {
                error::from_errno().fatal(
                    "dupe " + it.value().field() +
                    " to " + fields::mk(it.key())); }
            it.value().close();
            it.value() = fd_t(it.key()); }
        /* Close anything we no longer want. */
        for (filename::diriter it(filename("/proc/self/fd"));
             !it.finished();
             it.next()) {
            auto elem(parsers::intparser<int>().match(it.filename()));
            if (elem.issuccess() &&
                elem != childwrite.fd &&
                elem != childread.fd &&
                elem.success() > 2 &&
                elem.success() != it.dirfd().fd &&
                !fds.haskey(elem.success()) &&
                (!RUNNING_ON_VALGRIND || elem.success() < 10000)) {
                ::close(elem.success()); } }
        char fromchildstr[16];
        sprintf(fromchildstr, "%d", childwrite.fd);
        args[1] = fromchildstr;
        char tochildstr[16];
        sprintf(tochildstr, "%d", childread.fd);
        args[2] = tochildstr;
        fuzzsched();
        /* Do the exec */
        assert(execv(args[0], (char **)args) < 0);
        error::from_errno().fatal("execve " + fields::mk(args[0])); }
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
    logmsg(loglevel::debug, "spawn service initial message " + rr.field());
    if (rr.isfailure()) {
        /* Can't receive initial message -> exec failed. */
        rr.failure().warn("execing spawnservice");
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
    auto res(new process(pid,
                         fromchild.success().read,
                         tochild.success().write));
    logmsg(loglevel::debug, "new child in " + fields::mk(res));
    return res; }

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
    logmsg(loglevel::debug,
           "signal " + fields::mk(this) + " with " + snr.field());
    auto t(mux.lock());
    if (res != Nothing) {
        /* Too late */
        logmsg(loglevel::debug, "already exited with " + res.field());
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
        logmsg(loglevel::debug, "signal sent");
        assert(sendres == sizeof(msg)); }
    else {
        /* Child died before we could send the signal. */
        logmsg(loglevel::debug, "child dead: signal lost");
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
        logmsg(loglevel::debug,
               fields::mk(this) + " is already dead " + res.field());
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
        logmsg(loglevel::debug,
               fields::mk(this) + " collected death " + res.field());
        mux.unlock(&t);
        return token(); }
    logmsg(loglevel::debug, fields::mk(this) + " is still alive");
    mux.unlock(&t);
    return Nothing; }

either<shutdowncode, signalnr>
process::join(token) {
    assert(res != Nothing);
    assert(nrsubs == 0);
    auto rr(res.just());
    if (::kill(-pid, SIGKILL) < 0) error::from_errno().fatal("kill controller");
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
    join(clientio::CLIENTIO); }

const fields::field &
process::field() const {
    auto rr = mux.trylocked<const fields::field *>(
        [this] (maybe<mutex_t::token> m) {
            if (m == Nothing) return &("<busy:" + mux.field() + ">");
            else return &res.field(); });
    return "<process:" + fields::mk(pid).nosep() + " res:" + *rr + ">"; } }
