#include "test2.H"

#include <sys/time.h>
#include <err.h>
#include <signal.h>
#include <unistd.h>

#include <valgrind/valgrind.h>

#include <sqlite3.h>

#include "filename.H"
#include "fuzzsched.H"
#include "gitversion.H"
#include "logging.H"
#include "main.H"
#include "profile.H"
#include "spawn.H"
#include "timedelta.H"
#include "walltime.H"

#include "either.tmpl"
#include "list.tmpl"
#include "map.tmpl"
#include "orerror.tmpl"
#include "pair.tmpl"
#include "test2.tmpl"

bool
running_on_valgrind() {
    class __running_on_vgrind {
    public: const bool val;
    public: __running_on_vgrind() : val(RUNNING_ON_VALGRIND) {} };
    __running_on_vgrind inner;
    return inner.val; }

static map<string, nnp<testmodule> > *
modules;

static testmodule __testmeta(
    "meta",
    list<filename>::mk("test2.H", "test2.C", "test2.tmpl"),
    testmodule::LineCoverage(64_pc),
    testmodule::BranchCoverage(46_pc),
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
                (strcmp(extension, ".c") &&
                 strcmp(extension, ".C") &&
                 strcmp(extension, ".H") &&
                 strcmp(extension, ".tmpl"))) {
                continue; }
            if (covered.get(filename(it.filename())) == Nothing) {
                logmsg(loglevel::error,
                       "no test for " + filename(it.filename()).field());
                failed = true; } }
        assert(!failed); });

void
testresultaccumulator::result(const testmodule &tm,
                              const string &testcase,
                              timedelta duration) {
    results.append(pair<string, string>(tm.name(), testcase), duration); }

void
testresultaccumulator::dump(const maybe<filename> &df) const {
    for (auto it(results.start()); !it.finished(); it.next()) {
        logmsg(loglevel::info,
               it->first().first().field() + "::" +
               it->first().second().field() + " -> " +
               it->second().field()); }
    if (df != Nothing) {
        auto now(walltime::now());
        sqlite3 *db;
        auto r(sqlite3_open_v2(df.just().str().c_str(),
                               &db,
                               SQLITE_OPEN_READWRITE,
                               NULL));
        if (r != SQLITE_OK) {
            error::sqlite.fatal("opening " + df.just().field() +
                                ": " + fields::mk(r)); }
        auto query(
            ("INSERT INTO results "
             "(testmodule, testname, duration, date, sha) VALUES "
             "(?, ?, ?, " +
                 fields::mk(now.asint()).nosep() + ", \"" GITVERSION "\");")
            .c_str());
        sqlite3_stmt *stmt;
        r = sqlite3_prepare_v2(
            db,
            query,
            -1,
            &stmt,
            NULL);
        if (r != SQLITE_OK) {
            error::sqlite.fatal("preparing insert statement: " +
                                fields::mk(r)); }
        char *errmsg;
        errmsg = NULL;
        r = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &errmsg);
        if (r != SQLITE_OK) {
            error::sqlite.fatal("begin transaction: " +
                                fields::mk(r) + ": " +
                                fields::mk(errmsg)); }
        assert(errmsg == NULL);
        for (auto it (results.start()); !it.finished(); it.next()) {
            r = sqlite3_bind_text(stmt,
                                  1,
                                  it->first().first().c_str(),
                                  -1,
                                  SQLITE_STATIC);
            if (r != SQLITE_OK) {
                error::sqlite.fatal("bind parameter 1: " + fields::mk(r)); }
            r = sqlite3_bind_text(stmt,
                                  2,
                                  it->first().second().c_str(),
                                  -1,
                                  SQLITE_STATIC);
            if (r != SQLITE_OK) {
                error::sqlite.fatal("bind parameter 2: " + fields::mk(r)); }
            r = sqlite3_bind_int64(stmt,
                                   3,
                                   it->second().as_nanoseconds());
            if (r != SQLITE_OK) {
                error::sqlite.fatal("bind parameter 3: " + fields::mk(r)); }
            r = sqlite3_step(stmt);
            if (r != SQLITE_DONE) {
                error::sqlite.fatal("inserting record: " + fields::mk(r)); }
            r = sqlite3_reset(stmt);
            if (r != SQLITE_OK) {
                error::sqlite.fatal("resetting query: " + fields::mk(r)); } }
        r = sqlite3_exec(db, "END TRANSACTION", NULL, NULL, &errmsg);
        if (r != SQLITE_OK) {
            error::sqlite.fatal("end transaction: " +
                                fields::mk(r) + ": " +
                                fields::mk(errmsg)); }
        assert(errmsg == NULL);
        r = sqlite3_finalize(stmt);
        if (r != SQLITE_OK) {
            error::sqlite.fatal("finalise query: " + fields::mk(r)); }
        r = sqlite3_close(db);
        if (r != SQLITE_OK) {
            error::sqlite.fatal("close database: " + fields::mk(r)); } } }

testmodule::TestFlags::TestFlags(unsigned _flags) : flags(_flags) {}

bool
testmodule::TestFlags::intersects(TestFlags o) const {
    return (flags & o.flags) != 0; }

const fields::field &
testmodule::TestFlags::field() const {
    if (flags == dflt().flags) return fields::mk("");
    else if (flags == noauto().flags) return fields::mk("noauto");
    else return "<badflags:"+fields::mk(flags)+">"; }

testmodule::TestFlags
testmodule::TestFlags::dflt() {
    const testmodule::TestFlags res(0);
    return res; }

testmodule::TestFlags
testmodule::TestFlags::noauto() {
    const testmodule::TestFlags res(1);
    return res; }

testmodule::TestCase::TestCase(TestFlags _flags,
                               const string &_name,
                               const std::function<void ()> &_work)
    : flags(_flags),
      name(_name),
      work(_work) {}

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
                fields::padright(it.key().field(), 20) +
                fields::padright(it.value().flags.field(), 20)).c_str()); } }

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
testmodule::runtest(const string &what,
                    maybe<timedelta> limit,
                    testresultaccumulator &results) const {
    printf("%s\n",
           (fields::padright(name().field(), 20) +
            fields::padright(what.field(), 20)).c_str());
    if (limit != Nothing) alarm((unsigned)((TIMEDILATE * limit.just()) / 1_s));
    logmsg(loglevel::debug,
           "start test " + name().field() + "::" + what.field());
    auto tt(tests.getptr(what));
    if (tt == NULL) {
        error::notfound.fatal(
            "no such test: " + name().field() + "::" + what.field()); }
    auto timetaken(timedelta::time([tt] { tt->work(); }));
    logmsg(loglevel::debug,
           "pass test " + name().field() + "::" + what.field() +
           " in " + timetaken.field());
    if (limit != Nothing) alarm(0);
    results.result(*this, what, timetaken);
    tmpheap::release(); }

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
testmodule::runtests(maybe<timedelta> limit,
                     testresultaccumulator &results) const {
    list<string> schedule;
    for (auto it(tests.start()); !it.finished(); it.next()) {
        if (!it.value().flags.intersects(TestFlags::noauto())) {
            schedule.pushtail(it.key()); } }
    list<string> shuffled(shuffle(Steal, schedule));
    for (auto it(shuffled.start()); !it.finished(); it.next()) {
        runtest(*it, limit, results); } }

void
testmodule::prepare() const {
    filename("tmp").mkdir();
    if (!dependencies.empty()) {
        initpubsub();
        spawn::program p("/usr/bin/make");
        p.addarg("-j");
        p.addarg("8");
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
    for (auto it(files().start()); !it.finished(); it.next()) {
        mkverbose(*it); }
    mkverbose(filename(string("tests/unit/test") + name() + string(".C"))); }

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
    maybe<filename> database(Nothing);
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
        else if (args.peekhead() == "--database") {
            args.drophead();
            database.mkjust(args.pophead()); }
        else break; }
    if (args.empty()) listmodules();
    else if (args.idx(0) == "*") {
        if (args.length() != 1 && (args.length() != 2 || args.idx(1) != "*")) {
            errx(1,
                 "cannot specify a specific test without "
                 "specifying a specific module"); }
        testresultaccumulator acc;
        for (auto it(modules->start()); !it.finished(); it.next()) {
            if (args.length() == 2) {
                it.value()->prepare();
                it.value()->runtests(timeout, acc); }
            else it.value()->listtests(); }
        acc.dump(database); }
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
    else if (args.idx(0) == "freshdatabase") {
        if (database == Nothing) errx(1, "need a --database for freshdatabase");
        {   auto e(database.just().unlink());
            if (e.isfailure() && e.failure() != error::already) {
                e.fatal("removing " + database.just().field()); } }
        sqlite3 *db;
        auto r(sqlite3_open_v2(database.just().str().c_str(),
                               &db,
                               SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,
                               NULL));
        if (r != SQLITE_OK) {
            error::sqlite.fatal("opening " + database.just().field() +
                                ": " + fields::mk(r)); }
        char *errmsg;
        errmsg = NULL;
        r = sqlite3_exec(db,
                         "CREATE TABLE results "
                         "(testmodule NOT NULL, "
                         "testname NOT NULL, "
                         "duration INT NOT NULL, "
                         "date INT NOT NULL, "
                         "sha NOT NULL);",
                         NULL,
                         NULL,
                         &errmsg);
        if (r != SQLITE_OK) {
            error::sqlite.fatal("create table: " +
                                fields::mk(r) + ": " +
                                fields::mk(errmsg)); }
        r = sqlite3_close(db);
        if (r != SQLITE_OK) {
            error::sqlite.fatal("close database: " + fields::mk(r)); } }
    else {
        auto &module(*modules->get(args.idx(0))
                     .fatal("no such module: " + fields::mk(args.idx(0))));
        if (args.length() == 1) {
            if (stat) module.printmodule();
            else module.listtests(); }
        else if (args.length() == 2) {
            module.prepare();
            testresultaccumulator acc;
            if (args.idx(1) != "*") module.runtest(args.idx(1), timeout, acc);
            else module.runtests(timeout, acc);
            acc.dump(database); }
        else errx(1, "too many arguments"); }
    stopprofiling();
    return Success; }
