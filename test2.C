#include "test2.H"

#include <sys/time.h>
#include <err.h>
#include <signal.h>
#include <unistd.h>

#include "filename.H"
#include "logging.H"
#include "profile.H"

#include "list.tmpl"
#include "map.tmpl"

#include "fieldfinal.H"

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
testmodule::runtest(const string &what) const {
    printf("%s\n",
           (fields::padright(name.field(), 20) +
            fields::padright(what.field(), 20)).c_str());
    alarm(30);
    tests.get(what)
        .fatal("no such test: " + name.field() + "::" + what.field())
        ();
    alarm(0); }

void
testmodule::runtests() const {
    for (auto it(tests.start()); !it.finished(); it.next()) {
        runtest(it.key()); } }

int
main(int argc, char *argv[]) {
    struct timeval now;
    gettimeofday(&now, NULL);
    printf("Seed: %lx\n", now.tv_usec);
    srandom((unsigned)now.tv_usec);
    
    signal(SIGPIPE, SIG_IGN);
    
    bool stat = false;
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
        else break; }
    if (argc == 1) listmodules();
    else if (!strcmp(argv[1], "*")) {
        if (argc != 2 && (argc != 3 || strcmp(argv[2], "*"))) {
            errx(1,
                 "cannot specify a specific test without "
                 "specifying a specific module"); }
        for (auto it(modules->start()); !it.finished(); it.next()) {
            if (argc == 3) it.value()->runtests();
            else it.value()->listtests(); } }
    else {
        auto &module(*modules->get(argv[1])
                     .fatal("no such module: " + fields::mk(argv[1])));
        if (argc == 2) {
            if (stat) module.printmodule();
            else module.listtests(); }
        else if (argc == 3) {
            if (strcmp(argv[2], "*")) module.runtest(argv[2]);
            else module.runtests(); }
        else errx(1, "too many arguments"); }
    stopprofiling();
    return 0; }
