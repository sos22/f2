#include "test.H"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <functional>

#include "fields.H"
#include "filename.H"
#include "list.H"
#include "peername.H"
#include "shutdown.H"
#include "util.H"
#include "waitbox.H"

#include "list.tmpl"
#include "test.tmpl"

namespace tests {

#if TESTING
tests::eventwaiter<void>::eventwaiter(
    tests::event<void> &_evt,
    std::function<void ()> _action)
    : eventwaiter<void *>(_evt,
                          [_action] (void *) { _action(); }) {}
#endif

/* Careful: the ``real'' template arguments for the actual instance
 * we're called on don't necessarily match the static ones. */
template <> void
tests::hookpoint<void>::wait() {
    /* This could in theory wait quite a while without a proper
     * clientio token, if someone's specified a test hook which takes
     * a long time (and from a destructor at that!).  It's probably
     * good enough for testing infrastructure. */
    auto token(mux.lock());
    while (refcount != 0) token = idle.wait(clientio::CLIENTIO, &token);
    mux.unlock(&token); }

/* Careful: the ``real'' template arguments for the actual instance
 * we're called on don't necessarily match the static ones. */
template <> void
tests::hookpoint<void>::set(tests::hook<void> *what) {
    if (what == NULL) {
        assert(hooked != Nothing);
        wait();
        /* wait() isn't quite a full memory barrier.  Do an explicit
         * one here, for general sanity. */
        mb();
        hooked = Nothing; }
    else {
        assert(hooked == Nothing);
        mb();
        hooked = what; } }

struct test {
    const char *name;
    std::function<void ()> doit;
};

struct testcomponent {
    const char *name;
    list<test> tests;
};

static list<testcomponent> components;

void
testcaseV(const char *c_name,
          const char *t_name,
          std::function<void ()> doit) {
    testcomponent *component;
    component = NULL;
    for (auto it(components.start()); !it.finished(); it.next()) {
        if (!strcmp(it->name, c_name)) {
            component = &*it;
            break; } }
    if (!component) {
        component = &components.append();
        component->name = c_name; }
    for (auto it(component->tests.start()); !it.finished(); it.next()) {
        assert(strcmp(it->name, t_name)); }
    test *t = &component->tests.append();
    t->name = t_name;
    t->doit = doit; }

void
testcaseIO(const char *component,
           const char *test,
           std::function<void (clientio)> doit) {
    return testcaseV(component,
                     test,
                     [doit] {
                         initpubsub();
                         doit(clientio::CLIENTIO);
                         deinitpubsub(clientio::CLIENTIO); }); }

void listcomponents() {
    for (auto it(components.start()); !it.finished(); it.next()) {
        printf("%s\n", it->name); } }

void listtests(const char *component) {
    for (auto it(components.start()); !it.finished(); it.next()) {
        if (strcmp(it->name, component)) continue;
        for (auto it2(it->tests.start()); !it2.finished(); it2.next()) {
            printf("%s\n", it2->name); } } }

void runtest(const char *component, const char *t) {
    bool foundcomp = false;
    for (auto it(components.start()); !it.finished(); it.next()) {
        if (strcmp(component, "*") && strcmp(it->name, component)) continue;
        foundcomp = true;
        bool foundtest = false;
        for (auto it2(it->tests.start()); !it2.finished(); it2.next()) {
            if (strcmp(t, "*") && strcmp(it2->name, t)) continue;
            printf("%15s %60s\n", it->name, it2->name);
            foundtest = true;
            it2->doit(); }
        if (!foundtest) {
            error::notfound.fatal(
                "cannot find test " + fields::mk(t) + "::" +
                fields::mk(component)); } }
    if (!foundcomp) {
        error::notfound.fatal(
            "cannot find test component " + fields::mk(component)); } }

} /* End namespace tests */
