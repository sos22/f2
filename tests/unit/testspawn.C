#include <sys/types.h>
#include <signal.h>

#include "logging.H"
#include "spawn.H"
#include "testassert.H"
#include "test2.H"
#include "timedelta.H"

#include "either.tmpl"
#include "orerror.tmpl"
#include "testassert.tmpl"
#include "test2.tmpl"
#include "timedelta.tmpl"

using namespace spawn;

static buffer
readfd(clientio io, fd_t f) {
    buffer res;
    for (auto e(res.receive(io, f));
         e != error::disconnected;
         e = res.receive(io, f)) {
        e.fatal("reading from child"); }
    f.close();
    /* Make sure it's nul terminated. */
    res.queue("", 1);
    return res; }

static testmodule __spawntest(
    "spawn",
    list<filename>::mk("spawn.C", "spawn.H", "spawnservice.c"),
    testmodule::Dependency("spawnservice" EXESUFFIX),
    testmodule::Dependency("tests/abort/abort"),
    /* Coverage looks quite low on these tests, partly because gcov
     * can't collect coverage for anything which calls exec() or dies
     * with a signal. */
    testmodule::LineCoverage(75_pc),
    testmodule::BranchCoverage(50_pc),
    "truefalsebad", [] (clientio io) {
        {   auto p(process::spawn(program(filename("/bin/true")))
                   .fatal("spawning /bin/true"));
            (100_ms).future().sleep(io);
            assert(p->hasdied() != Nothing);
            assert(p->join(p->hasdied().just()).left() ==shutdowncode::ok);}
        {   auto p(process::spawn(program("/bin/false"))
                   .fatal("spawning /bin/false"));
            (100_ms).future().sleep(io);
            assert(p->hasdied() != Nothing);
            assert(p->join(p->hasdied().just()).left() == shutdowncode(1));}
        {   auto p(process::spawn(program("/does/not/exist")));
            assert(p.isfailure());
            assert(p == error::from_errno(ENOENT)); } },
    "hasdiednoblock", [] {
        auto p(process::spawn(program("/bin/sleep").addarg("1"))
               .fatal("sleep 1"));
        int cntr(0);
        while (p->hasdied() == Nothing) cntr++;
        tassert(T(cntr) >= T(200));
        assert(p->join(p->hasdied().just()).left() == shutdowncode::ok); },
    "sleep", [] {
        auto start(timestamp::now());
        string argstr("1");
        auto p(process::spawn(program("/bin/sleep")
                              .addarg(argstr))
               .fatal("spawning /bin/sleep"));
        while (p->hasdied() == Nothing) {}
        assert(p->join(p->hasdied().just()).left() == shutdowncode::ok);
        auto t(timestamp::now() - start - timedelta::seconds(1));
        assert(t >= timedelta::seconds(0));
        tassert(T(t) <= T(timedelta::milliseconds(200))); },
    "sleep2", [] (clientio io) {
        auto start(timestamp::now());
        auto p(process::spawn(program("/bin/sleep").addarg("1"))
               .fatal("spawning /bin/sleep"));
        {   subscriber ss;
            spawn::subscription sub(ss, *p);
            int woke;
            woke = 0;
            while (p->hasdied() == Nothing) {
                woke++;
                assert(ss.wait(io) == &sub);
                sub.rearm(); }
            /* Can get a few spurious wakes, but not too many. */
            assert(woke < 5); }
        tassert(T(p->join(p->hasdied().just()).left()) ==
                T(shutdowncode::ok));
        auto t(timestamp::now() - start -
               (timedelta::seconds(1) / (running_on_valgrind()
                                         ? VALGRIND_TIMEWARP
                                         : 1)));
        tassert(T(t) >= T(0_s));
        tassert(T(t) <= T(300_ms)); },
    "sleep3", [] (clientio) {
        auto start(timestamp::now());
        auto p(process::spawn(program("/bin/sleep").addarg(string("1")))
               .fatal("spawning /bin/sleep"));
        assert(p->join(clientio::CLIENTIO).left() == shutdowncode::ok);
        auto t(timestamp::now() - start - timedelta::seconds(1));
        tassert(T(t) >= T(0_s));
        tassert(T(t) <= T(300_ms)); },
    "sleep4", [] (clientio io) {
        auto p(process::spawn(program("/bin/sleep")
                              .addarg(std::move(string("1"))))
               .fatal("spawning /bin/sleep"));
        (2_s).future().sleep(io);
        {   subscriber ss;
            spawn::subscription sub(ss, *p);
            assert(ss.poll() == &sub);
            assert(p->hasdied() != Nothing);
            assert(ss.poll() == NULL);
            sub.rearm();
            assert(ss.poll() == &sub); }
        assert(p->join(io).left() == shutdowncode::ok); },
    "signal", [] (clientio io) {
        auto p(process::spawn(program("/bin/sleep").addarg(fields::mk(1)))
               .fatal("spawning"));
        assert(p->hasdied() == Nothing);
        p->signal(signalnr::kill);
        (50_ms).future().sleep(io);
        assert(p->join(p->hasdied().just()).right() == signalnr::kill); },
    "signal2", [] (clientio io) {
        auto p(process::spawn(program("/bin/sleep").addarg("1"))
               .fatal("spawning"));
        assert(p->hasdied() == Nothing);
        p->signal(signalnr::term);
        (50_ms).future().sleep(io);
        assert(p->join(p->hasdied().just()).right() == signalnr::term); },
    "signal3", [] (clientio io) {
        logmsg(loglevel::debug, "starting sleep");
        auto p(process::spawn(program("/bin/sleep").addarg("1"))
               .fatal("spawning"));
        logmsg(loglevel::debug, "started sleep");
        (500_ms).future().sleep(io);
        assert(p->hasdied() == Nothing);
        p->signal(signalnr::stop);
        logmsg(loglevel::debug, "stopped sleep");
        (2_s).future().sleep(io);
        assert(p->hasdied() == Nothing);
        p->signal(signalnr::cont);
        logmsg(loglevel::debug, "continued sleep");
        (1500_ms).future().sleep(io);
        logmsg(loglevel::debug, "sleep should be finished");
        assert(p->join(p->hasdied().just()).left() == shutdowncode::ok); },
    "signal4", [] (clientio io) {
        auto p(process::spawn(program("/bin/true")).fatal("spawning truth"));
        (50_ms).future().sleep(io);
        p->signal(signalnr::kill);
        assert(p->join(p->hasdied().just()).left() == shutdowncode::ok); },
    "signal5", [] (clientio io) {
        auto p(process::spawn(program("/bin/true")).fatal("spawning truth"));
        (50_ms).future().sleep(io);
        assert(p->hasdied() != Nothing);
        p->signal(signalnr::kill);
        assert(p->join(p->hasdied().just()).left() == shutdowncode::ok); },
    "raise1", [] (clientio io) {
        auto p(process::spawn(program("./tests/abort/abort"))
               .fatal("spawing ./tests/abort/abort"));
        assert(p->join(io).right() == signalnr::abort); },
    "raise2", [] (clientio io) {
        auto p(process::spawn(program("./tests/abort/abort"))
               .fatal("spawing ./tests/abort/abort"));
        (50_ms).future().sleep(io);
        p->signal(signalnr::kill);
        assert(p->join(io).right() == signalnr::abort); },
    "internal", [] {
        assert(signalnr::abort.internallygenerated());
        assert(!signalnr::kill.internallygenerated()); },
    "pause", [] (clientio io) {
        auto p(process::spawn(program("/bin/sleep").addarg(".11"))
               .fatal("spawning sleep"));
        auto tok(p->pause());
        (200_ms).future().sleep(io);
        assert(p->hasdied() == Nothing);
        p->unpause(tok);
        (200_ms).future().sleep(io);
        assert(p->join(p->hasdied().just()).left() == shutdowncode::ok); },
    "pause2", [] (clientio io) {
        auto p(process::spawn(program("/bin/sleep").addarg(".2"))
               .fatal("spawning sleep"));
        (500_ms).future().sleep(io);
        auto tok(p->pause());
        assert(p->hasdied() != Nothing);
        p->unpause(tok);
        assert(p->join(p->hasdied().just()).left() == shutdowncode::ok); },
    "pause3", [] (clientio io) {
        auto p(process::spawn(program("/bin/sleep").addarg(".2"))
               .fatal("spawning sleep"));
        (500_ms).future().sleep(io);
        assert(p->hasdied() != Nothing);
        auto tok(p->pause());
        assert(p->hasdied() != Nothing);
        p->unpause(tok);
        assert(p->join(p->hasdied().just()).left() == shutdowncode::ok); },
    "pause4", [] (clientio io) {
        auto p(process::spawn(program("./tests/abort/abort"))
               .fatal("spawning abort"));
        (50_ms).future().sleep(io);
        auto tok(p->pause());
        p->unpause(tok);
        assert(p->join(p->hasdied().just()).right() == signalnr::abort); },
    "kill", [] (clientio) {
        auto p(process::spawn(program("/bin/sleep").addarg("3600"))
               .fatal("spawning sleep"));
        auto start(timestamp::now());
        p->kill();
        auto end(timestamp::now());
        tassert(T(end) - T(start) < T(200_ms)); },
    "fd", [] (clientio io) {
        auto inpipe(fd_t::pipe().fatal("in pipe"));
        auto outpipe(fd_t::pipe().fatal("out pipe"));
        auto p(process::spawn(program("/usr/bin/tr")
                              .addarg("[:upper:]")
                              .addarg("[:lower:]")
                              .addfd(inpipe.read, 0)
                              .addfd(outpipe.write, 1))
               .fatal("spawn tr"));
        inpipe.read.close();
        outpipe.write.close();
        inpipe.write.write(io, "HELLO", 5).fatal("send hello");
        inpipe.write.close();
        char buf[7];
        assert(outpipe.read.read(io, buf, 7).fatal("reading back") == 5);
        assert(!memcmp(buf, "hello", 5));
        assert(p->join(io).left() == shutdowncode::ok);
        outpipe.read.close(); },
    "run", [] (clientio io) {
        assert(program("/bin/true")
               .run(io)
               .fatal("true")
               .left() == shutdowncode::ok);
        assert(program("/bin/false")
               .run(io)
               .fatal("false")
               .left() != shutdowncode::ok);
        auto p(fd_t::pipe().fatal("mkpipe"));
        assert(program("/bin/echo")
               .addarg("hello")
               .addfd(p.write, 1)
               .run(io)
               .fatal("echo")
               .left() == shutdowncode::ok);
        p.write.close();
        char buf[7];
        assert(p.read.read(io, buf, 7).fatal("reading back") == 6);
        assert(!memcmp(buf, "hello\n", 6));
        p.read.close(); },
    "fdleak", [] (clientio io) {
        /* Check that we're not leaking any unexpected FDs into the
         * child. */
        auto outpipe(fd_t::pipe().fatal("out pipe"));
        /* Make sure there are some FDs for us to leak. :) */
        auto bonuspipe(fd_t::pipe().fatal("bonus pipe"));
        auto p(process::spawn(program("/bin/ls")
                              .addarg("/proc/self/fd")
                              .addfd(outpipe.write, 1))
               .fatal("spawn ls"));
        bonuspipe.close();
        outpipe.write.close();
        auto lsres(readfd(io, outpipe.read));
        assert(p->join(io).left() == shutdowncode::ok);
        auto str = (char *)lsres.linearise();
        /* The only things open are stdin (0), the outpipe (1), stderr
         * (2), and the fd ls opens /proc/self/fd (3) */
        assert(!strcmp(str, "0\n1\n2\n3\n")); },
    "fdswap", [] (clientio io) {
        /* Check that swapping FDs works, since that's a bit more fiddly. */
        auto pipe1(fd_t::pipe().fatal("pipe1"));
        auto pipe2(fd_t::pipe().fatal("pipe2"));
        auto p(process::spawn(program("/bin/sh")
                              .addarg("-c")
                              .addarg(
                                  "echo pipe2 >&" + fields::mk(pipe2.write.fd) +
                                  ";echo pipe1 >&"+ fields::mk(pipe1.write.fd))
                              .addfd(pipe1.write, pipe2.write.fd)
                              .addfd(pipe2.write, pipe1.write.fd))
               .fatal("spawn sh"));
        pipe1.write.close();
        pipe2.write.close();
        auto pipe1r(readfd(io, pipe1.read));
        auto pipe2r(readfd(io, pipe2.read));
        assert(!strcmp((char *)pipe1r.linearise(), "pipe2\n"));
        assert(!strcmp((char *)pipe2r.linearise(), "pipe1\n"));
        pipe1.read.close();
        pipe2.read.close();
        assert(p->join(io).left() == shutdowncode::ok); },
    "forked", [] (clientio io) {
        /* Make sure that killing process groups works as expected. */
        auto pipe1(fd_t::pipe().fatal("pipe1"));
        auto pipe2(fd_t::pipe().fatal("pipe2"));
        auto &p(*process::spawn(program("/bin/sh")
                                .addarg("-c")
                                .addarg("/bin/cat")
                                .addfd(pipe1.read, 0)
                                .addfd(pipe2.write, 1))
                .fatal("spawn sh"));
        pipe1.read.close();
        pipe2.write.close();
        p.kill();
        char buf[16];
        assert(pipe2.read.read(io, buf, sizeof(buf)) == error::disconnected);
        pipe2.read.close();
        pipe1.write.close(); },
    "progfield", [] {
#define tt(pp, expected) do {                                           \
            auto c((pp).field().c_str());                               \
            if (strcmp(c, expected)) {                                  \
                logmsg(loglevel::emergency,                             \
                       fields::mk(c) + " != " + fields::mk(expected));  \
                abort();                                                \
            };                                                          \
        } while (0)
        tt(spawn::program("hello"), "{hello}");
        tt(spawn::program("hello")
           .addarg("goodbye"),
           "{hello goodbye}");
        tt(spawn::program("hello")
           .addarg("goodbye")
           .addarg("world"),
           "{hello goodbye world}");
        tt(spawn::program("hello")
           .addarg("goodbye")
           .addarg("world")
           .addarg(fields::mk(5)),
           "{hello goodbye world 5}");
        tt(spawn::program("hello")
           .addarg("goodbye")
           .addarg("world")
           .addarg(fields::mk(5))
           .addfd(fd_t(7), 8),
           "{hello goodbye world 5 8->7}");
#undef tt
    },
    "managerdied", [] (clientio io) {
        /* The manager dying unexpectedly should be a failure. */
        auto &p(*process::spawn(program("/bin/sleep")
                                .addarg("999999"))
                .fatal("sleep"));
        ::kill(p.managerpid(), SIGKILL);
        assert(p.join(io).left() == shutdowncode::managerdied); } );
