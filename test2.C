#include "test2.H"

#include <sys/time.h>
#include <err.h>
#include <signal.h>
#include <unistd.h>

#include "filename.H"
#include "fuzzsched.H"
#include "logging.H"
#include "main.H"
#include "profile.H"
#include "spawn.H"
#include "timedelta.H"

#include "either.tmpl"
#include "list.tmpl"
#include "map.tmpl"
#include "orerror.tmpl"
#include "test2.tmpl"

static map<string, nnp<testmodule> > *
modules;

static testmodule __testmeta(
    "meta",
    list<filename>::mk("test2.H", "test2.C", "test2.tmpl"),
    testmodule::LineCoverage(64_pc),
    testmodule::BranchCoverage(49_pc),
    "nodupes", [] {
        /* No file should be tested by multiple modules. */
        map<filename, string> covered;
        bool failed = false;
        for (auto it(modules->start()); !it.finished(); it.next()) {
            for (auto it2(it.value()->files().start());
                 !it2.finished();
                 it2.next()) {
                auto g(covered.get(*it2));
                if (g != Nothing) {
                    logmsg(loglevel::error,
                           it2->field() + " is covered by " +
                           it.value()->name().field() + " and " +
                           g.just().field());
                    failed = true; }
                else covered.set(*it2, it.value()->name()); } }
        assert(!failed); },
    "nomissing", [] {
        /* Every file covered must actually exist. */
        bool failed = false;
        for (auto it(modules->start()); !it.finished(); it.next()) {
            for (auto it2(it.value()->files().start());
                 !it2.finished();
                 it2.next()) {
                auto r(it2->isfile());
                if (r != true) {
                    logmsg(loglevel::error,
                           it.value()->name().field() + " tests " +
                           it2->field() + " which does not exist: " +
                           r.field());
                    failed = true; } } }
        assert(!failed); },
    "testeverything", [] {
        /* Every file must be covered by a test. */
        map<filename, bool> covered;
        for (auto it(modules->start()); !it.finished(); it.next()) {
            for (auto it2(it.value()->files().start());
                 !it2.finished();
                 it2.next()) {
                covered.set(*it2, true); } }
        bool failed = false;
        for (filename::diriter it(filename(".")); !it.finished(); it.next()) {
            auto extension(strrchr(it.filename(), '.'));
            if (extension == NULL ||
                (strcmp(extension, ".C") &&
                 strcmp(extension, ".H") &&
                 strcmp(extension, ".tmpl"))) {
                continue; }
            if (covered.get(filename(it.filename())) == Nothing) {
                logmsg(loglevel::error,
                       "no test for " + filename(it.filename()).field());
                failed = true; } }
        assert(!failed); });

void
testmodule::applyinit() {
    if (modules == NULL) modules = new map<string, nnp<testmodule> >();
    modules->set(name(), _nnp(*this)); }

static void
listmodules(void) {
    for (auto it(modules->start()); !it.finished(); it.next()) {
        it.value()->printmodule(); } }

void
testmodule::listtests() const {
    for (auto it(tests.start()); !it.finished(); it.next()) {
        printf("%s\n",
               (fields::padright(fields::mk(name()), 20) +
                fields::padright(it.key().field(), 20)).c_str()); } }

void
testmodule::printmodule() const {
    printf("Module: %s\n", name().field().c_str());
    printf("    Line coverage:   %s\n",
           fields::padright(linecoverage.p.field(), 10).c_str());
    printf("    Branch coverage: %s\n",
           fields::padright(branchcoverage.p.field(), 10).c_str());
    for (auto it(files().start()); !it.finished(); it.next()) {
        printf("    File:            %s\n", it->str().c_str()); } }

void
testmodule::runtest(const string &what, maybe<timedelta> limit) const {
    printf("%s\n",
           (fields::padright(name().field(), 20) +
            fields::padright(what.field(), 20)).c_str());
    if (limit != Nothing) alarm((unsigned)(limit.just() / 1_s));
    logmsg(loglevel::debug, "start test " + name().field());
    tests.get(what)
        .fatal("no such test: " + name().field() + "::" + what.field())
        ();
    logmsg(loglevel::debug, "pass test " + name().field());
    if (limit != Nothing) alarm(0); }

static list<string>
shuffle(_Steal s, list<string> &what) {
    list<string> res;
    unsigned len(what.length());
    while (len != 0) {
        unsigned idx((unsigned)random() % len);
        auto &elem(what.idx(idx));
        res.append(s, elem);
        what.drop(&elem);
        len--; }
    assert(what.empty());
    return res; }

void
testmodule::runtests(maybe<timedelta> limit) const {
    list<string> schedule;
    for (auto it(tests.start()); !it.finished(); it.next()) {
        schedule.pushtail(it.key()); }
    list<string> shuffled(shuffle(Steal, schedule));
    for (auto it(shuffled.start()); !it.finished(); it.next()) {
        runtest(*it, limit); } }

void
testmodule::prepare() const {
    filename("tmp").mkdir();
    if (dependencies.empty()) return;
    initpubsub();
    spawn::program p("/usr/bin/make");
    for (auto it(dependencies.start()); !it.finished(); it.next()) {
        p.addarg(it->str()); }
    auto res(spawn::process::spawn(p)
             .fatal("starting make")
             ->join(clientio::CLIENTIO));
    if (res.isright()) {
        fprintf(stderr,
                "make died with signal %s\n",
                res.right().field().c_str());
        exit(1); }
    else if (res.left() != shutdowncode::ok) {
        fprintf(stderr,
                "make failed with code %s\n",
                fields::mk(res.left()).c_str());
        exit(1); }
    deinitpubsub(clientio::CLIENTIO); }

static void
_alarm(int) { abort(); }

/* Get a core dump whenever a test fails. */
static void
exithandler(int code, void *) { if (code != 0) abort(); }

orerror<void>
f2main(list<string> &args) {
    struct timeval now;
    gettimeofday(&now, NULL);
    printf("Seed: %lx\n", now.tv_usec);
    srandom((unsigned)now.tv_usec);
    
    signal(SIGPIPE, SIG_IGN);
    signal(SIGALRM, _alarm);
    
    on_exit(::exithandler, NULL);
    
    bool stat = false;
    maybe<timedelta> timeout(30_s);
    while (!args.empty()) {
        if (args.peekhead() == "--profile") {
            startprofiling();
            args.drophead(); }
        else if (args.peekhead() == "--stat") {
            stat = true;
            args.drophead(); }
        else if (args.peekhead() == "--notimeouts") {
            timeout = Nothing;
            args.drophead(); }
        else if (args.peekhead() == "--fuzzsched") {
            __do_fuzzsched = true;
            args.drophead(); }
        else break; }
    if (args.empty()) listmodules();
    else if (args.idx(0) == "*") {
        if (args.length() != 1 && (args.length() != 2 || args.idx(1) != "*")) {
            errx(1,
                 "cannot specify a specific test without "
                 "specifying a specific module"); }
        for (auto it(modules->start()); !it.finished(); it.next()) {
            if (args.length() == 2) {
                it.value()->prepare();
                it.value()->runtests(timeout); }
            else it.value()->listtests(); } }
    else if (args.idx(0) == "findmodule") {
        if (args.length() != 2) errx(1, "need a filename to find a module for");
        filename f(args.idx(1));
        for (auto it(modules->start()); !it.finished(); it.next()) {
            for (auto it2(it.value()->files().start());
                 !it2.finished();
                 it2.next()) {
                if (*it2 == f) {
                    printf("%s\n", it.key().c_str());
                    exit(0); } } }
        errx(1, "no test module for file %s", args.idx(1).c_str()); }
    else {
        auto &module(*modules->get(args.idx(0))
                     .fatal("no such module: " + fields::mk(args.idx(0))));
        if (args.length() == 1) {
            if (stat) module.printmodule();
            else module.listtests(); }
        else if (args.length() == 2) {
            module.prepare();
            if (args.idx(1) != "*") module.runtest(args.idx(1), timeout);
            else module.runtests(timeout); }
        else errx(1, "too many arguments"); }
    stopprofiling();
    return Success; }
