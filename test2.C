#include "test2.H"

#include <sys/time.h>
#include <err.h>
#include <signal.h>
#include <unistd.h>

#include "filename.H"
#include "fuzzsched.H"
#include "logging.H"
#include "profile.H"
#include "spawn.H"
#include "timedelta.H"

#include "either.tmpl"
#include "list.tmpl"
#include "map.tmpl"

static map<string, nnp<testmodule> > *
modules;

void
testmodule::applyinit() {
    if (modules == NULL) modules = new map<string, nnp<testmodule> >();
    modules->set(name, _nnp(*this)); }

static void
listmodules(void) {
    for (auto it(modules->start()); !it.finished(); it.next()) {
        it.value()->printmodule(); } }

void
testmodule::listtests() const {
    for (auto it(tests.start()); !it.finished(); it.next()) {
        printf("%s\n",
               (fields::padright(fields::mk(name), 20) +
                fields::padright(it.key().field(), 20)).c_str()); } }

void
testmodule::printmodule() const {
    printf("Module: %s\n", fields::padright(name.field(), 20).c_str());
    printf("    Line coverage:   %s\n",
           fields::padright(linecoverage.p.field(), 10).c_str());
    printf("    Branch coverage: %s\n",
           fields::padright(branchcoverage.p.field(), 10).c_str());
    for (auto it(files.start()); !it.finished(); it.next()) {
        printf("    File:            %s\n", it->str().c_str()); } }

void
testmodule::runtest(const string &what, maybe<timedelta> limit) const {
    printf("%s\n",
           (fields::padright(name.field(), 20) +
            fields::padright(what.field(), 20)).c_str());
    if (limit != Nothing) alarm((unsigned)(limit.just() / 1_s));
    tests.get(what)
        .fatal("no such test: " + name.field() + "::" + what.field())
        ();
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

int
main(int argc, char *argv[]) {
    struct timeval now;
    gettimeofday(&now, NULL);
    printf("Seed: %lx\n", now.tv_usec);
    srandom((unsigned)now.tv_usec);
    
    signal(SIGPIPE, SIG_IGN);
    
    bool stat = false;
    maybe<timedelta> timeout(30_s);
    while (argc > 1) {
        if (!strcmp(argv[1], "--verbose")) {
            initlogging("tests");
            argv++;
            argc--; }
        else if (!strcmp(argv[1], "--profile")) {
            startprofiling();
            argv++;
            argc--; }
        else if (!strcmp(argv[1], "--stat")) {
            stat = true;
            argv++;
            argc--; }
        else if (!strcmp(argv[1], "--notimeouts")) {
            timeout = Nothing;
            argv++;
            argc--; }
        else if (!strcmp(argv[1], "--fuzzsched")) {
            __do_fuzzsched = true;
            argv++;
            argc--; }
        else break; }
    if (argc == 1) listmodules();
    else if (!strcmp(argv[1], "*")) {
        if (argc != 2 && (argc != 3 || strcmp(argv[2], "*"))) {
            errx(1,
                 "cannot specify a specific test without "
                 "specifying a specific module"); }
        for (auto it(modules->start()); !it.finished(); it.next()) {
            if (argc == 3) {
                it.value()->prepare();
                it.value()->runtests(timeout); }
            else it.value()->listtests(); } }
    else {
        auto &module(*modules->get(argv[1])
                     .fatal("no such module: " + fields::mk(argv[1])));
        if (argc == 2) {
            if (stat) module.printmodule();
            else module.listtests(); }
        else if (argc == 3) {
            module.prepare();
            if (strcmp(argv[2], "*")) module.runtest(argv[2], timeout);
            else module.runtests(timeout); }
        else errx(1, "too many arguments"); }
    stopprofiling();
    return 0; }
