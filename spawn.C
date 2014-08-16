#include "spawn.H"

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include "spawnservice.h"

#include "buildconfig.H"
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

program::~program() {
    args.flush(); }

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
    int nrargs(p.args.length());
    const char *args[nrargs + 3];
    int i(0);
    string servicename("spawnservice");
#if COVERAGE
    servicename = servicename + "-c";
#endif
    filename path(buildconfig::us.PREFIX + servicename);
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
    auto pid(::fork());
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
      res(Nothing) {}

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
        assert(msg.tag == message::msgchilddied);
        if (WIFEXITED(msg.childdied.status)) {
            res = either<shutdowncode, signalnr>::left(
                shutdowncode(WEXITSTATUS(msg.childdied.status))); }
        else {
            assert(WIFSIGNALED(msg.childdied.status));
            res = either<shutdowncode, signalnr>::right(
                signalnr(WTERMSIG(msg.childdied.status))); } }
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
        assert(msg.tag == message::msgchilddied);
        if (WIFEXITED(msg.childdied.status)) {
            res = either<shutdowncode, signalnr>::left(
                shutdowncode(WEXITSTATUS(msg.childdied.status))); }
        else {
            assert(WIFSIGNALED(msg.childdied.status));
            res = either<shutdowncode, signalnr>::right(
                signalnr(WTERMSIG(msg.childdied.status))); }
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
    return join(t.just()); } }

void
tests::_spawn() {
    using namespace spawn;
    testcaseV("spawn", "truefalsebad", [] {
            {   auto p(process::spawn(program(filename("/bin/true")))
                       .fatal("spawning /bin/true"));
                (timestamp::now() + timedelta::milliseconds(100)).sleep();
                assert(p->hasdied() != Nothing);
                assert(p->join(p->hasdied().just()).left() ==shutdowncode::ok);}
            {   auto p(process::spawn(program("/bin/false"))
                       .fatal("spawning /bin/false"));
                (timestamp::now() + timedelta::milliseconds(100)).sleep();
                assert(p->hasdied() != Nothing);
                assert(p->join(p->hasdied().just()).left() == shutdowncode(1));}
            {   auto p(process::spawn(program("/does/not/exist")));
                assert(p.isfailure());
                assert(p == error::from_errno(ENOENT)); }});
    testcaseV("spawn", "hasdiednoblock", [] {
            auto p(process::spawn(program("/bin/sleep")
                                  .addarg("1"))
                   .fatal("sleep 1"));
            int cntr(0);
            while (p->hasdied() == Nothing)
                cntr++;
            assert(cntr >= 500);
            assert(p->join(p->hasdied().just()).left() == shutdowncode::ok); });
    testcaseV("spawn", "sleep", [] {
            auto start(timestamp::now());
            string argstr("1");
            auto p(process::spawn(program("/bin/sleep")
                                  .addarg(argstr))
                   .fatal("spawning /bin/sleep"));
            while (p->hasdied() == Nothing) {}
            assert(p->join(p->hasdied().just())
                   .left() == shutdowncode::ok);
            auto t(timestamp::now() - start - timedelta::seconds(1));
            assert(t >= timedelta::seconds(0));
            assert(t <= timedelta::milliseconds(50)); });
    testcaseV("spawn", "sleep2", [] {
            initpubsub();
            auto start(timestamp::now());
            auto p(process::spawn(program("/bin/sleep")
                                  .addarg("1"))
                   .fatal("spawning /bin/sleep"));
            {   subscriber ss;
                spawn::subscription sub(ss, *p);
                int woke;
                woke = 0;
                while (p->hasdied() == Nothing) {
                    woke++;
                    assert(ss.wait(clientio::CLIENTIO) == &sub);
                    sub.rearm(); }
                /* Can get a few spurious wakes, but not too many. */
                assert(woke < 5); }
            assert(p->join(p->hasdied().just()).left() == shutdowncode::ok);
            auto t(timestamp::now() - start - timedelta::seconds(1));
            assert(t >= timedelta::seconds(0));
            assert(t <= timedelta::milliseconds(50));
            deinitpubsub(clientio::CLIENTIO); });
    testcaseV("spawn", "sleep3", [] {
            initpubsub();
            auto start(timestamp::now());
            auto p(process::spawn(program("/bin/sleep")
                                  .addarg(string("1")))
                   .fatal("spawning /bin/sleep"));
            assert(p->join(clientio::CLIENTIO)
                   .left() == shutdowncode::ok);
            auto t(timestamp::now() - start - timedelta::seconds(1));
            assert(t >= timedelta::seconds(0));
            assert(t <= timedelta::milliseconds(50));
            deinitpubsub(clientio::CLIENTIO); });
    testcaseV("spawn", "sleep4", [] {
            initpubsub();
            auto start(timestamp::now());
            auto p(process::spawn(program("/bin/sleep")
                                  .addarg(std::move(string("1"))))
                   .fatal("spawning /bin/sleep"));
            (timestamp::now() + timedelta::seconds(2)).sleep();
            {   subscriber ss;
                spawn::subscription sub(ss, *p);
                assert(ss.poll() == &sub);
                assert(p->hasdied() != Nothing);
                assert(ss.poll() == NULL);
                sub.rearm();
                assert(ss.poll() == &sub); }
            assert(p->join(clientio::CLIENTIO).left() == shutdowncode::ok);
            deinitpubsub(clientio::CLIENTIO); } );
    testcaseV("spawn", "signal", [] {
            auto p(process::spawn(program("/bin/sleep")
                                  .addarg(fields::mk(1)))
                   .fatal("spawning"));
            assert(p->hasdied() == Nothing);
            p->signal(signalnr::kill);
            (timestamp::now() + timedelta::milliseconds(50)).sleep();
            assert(p->join(p->hasdied().just()).right()== signalnr::kill); });
    testcaseV("spawn", "signal2", [] {
            auto p(process::spawn(program("/bin/sleep")
                                  .addarg("1"))
                   .fatal("spawning"));
            assert(p->hasdied() == Nothing);
            p->signal(signalnr::term);
            (timestamp::now() + timedelta::milliseconds(50)).sleep();
            assert(p->join(p->hasdied().just()).right()== signalnr::term); });
    testcaseV("spawn", "signal3", [] {
            auto p(process::spawn(program("/bin/sleep")
                                  .addarg("1"))
                   .fatal("spawning"));
            assert(p->hasdied() == Nothing);
            p->signal(signalnr::stop);
            (timestamp::now() + timedelta::seconds(2)).sleep();
            assert(p->hasdied() == Nothing);
            p->signal(signalnr::cont);
            (timestamp::now() + timedelta::milliseconds(50)).sleep();
            assert(p->join(p->hasdied().just()).left() ==shutdowncode::ok); });
    testcaseV("spawn", "signal4", [] {
            auto p(process::spawn(program("/bin/true"))
                   .fatal("spawning truth"));
            (timestamp::now() + timedelta::milliseconds(50)).sleep();
            p->signal(signalnr::kill);
            assert(p->join(p->hasdied().just()).left() ==shutdowncode::ok); });
    testcaseV("spawn", "signal5", [] {
            auto p(process::spawn(program("/bin/true"))
                   .fatal("spawning truth"));
            (timestamp::now() + timedelta::milliseconds(50)).sleep();
            assert(p->hasdied() != Nothing);
            p->signal(signalnr::kill);
            assert(p->join(p->hasdied().just()).left() ==shutdowncode::ok); } );
    testcaseV("spawn", "raise1", [] {
            initpubsub();
            auto p(process::spawn(program("./tests/abort/abort"))
                   .fatal("spawing ./tests/abort/abort"));
            assert(p->join(clientio::CLIENTIO).right() == signalnr::abort);
            deinitpubsub(clientio::CLIENTIO); });
    testcaseV("spawn", "raise2", [] {
            initpubsub();
            auto p(process::spawn(program("./tests/abort/abort"))
                   .fatal("spawing ./tests/abort/abort"));
            (timestamp::now() + timedelta::milliseconds(50)).sleep();
            p->signal(signalnr::kill);
            assert(p->join(clientio::CLIENTIO).right() == signalnr::abort);
            deinitpubsub(clientio::CLIENTIO); } ); }
