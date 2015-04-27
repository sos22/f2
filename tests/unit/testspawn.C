#include "spawn.H"
#include "test2.H"
#include "timedelta.H"

#include "test2.tmpl"
#include "timedelta.tmpl"

using namespace spawn;

static testmodule __spawntest(
    "spawn",
    list<filename>::mk("spawn.C", "spawn.H"),
    testmodule::Dependency("spawnservice-c"),
    testmodule::Dependency("tests/abort/abort"),
    /* Coverage looks quite low on these tests, partly because gcov
     * can't collect coverage for anything which calls exec() or dies
     * with a signal. */
    testmodule::LineCoverage(15_pc),
    testmodule::BranchCoverage(55_pc),
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
        assert(cntr >= 500);
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
        assert(t <= timedelta::milliseconds(50)); },
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
        assert(p->join(p->hasdied().just()).left() == shutdowncode::ok);
        auto t(timestamp::now() - start - timedelta::seconds(1));
        assert(t >= timedelta::seconds(0));
        assert(t <= timedelta::milliseconds(50)); },
    "sleep3", [] (clientio) {
        auto start(timestamp::now());
        auto p(process::spawn(program("/bin/sleep").addarg(string("1")))
               .fatal("spawning /bin/sleep"));
        assert(p->join(clientio::CLIENTIO).left() == shutdowncode::ok);
        auto t(timestamp::now() - start - timedelta::seconds(1));
        assert(t >= timedelta::seconds(0));
        assert(t <= timedelta::milliseconds(50)); },
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
        auto p(process::spawn(program("/bin/sleep").addarg("1"))
               .fatal("spawning"));
        assert(p->hasdied() == Nothing);
        p->signal(signalnr::stop);
        (2_s).future().sleep(io);
        assert(p->hasdied() == Nothing);
        p->signal(signalnr::cont);
        (1_s).future().sleep(io);
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
        assert(end - start < 50_ms); });
