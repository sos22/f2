#include "test.H"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <functional>

#include "controlserver.H"
#include "filename.H"
#include "list.H"
#include "peername.H"
#include "shutdown.H"
#include "waitbox.H"

#include "list.tmpl"
#include "orerror.tmpl"
#include "test.tmpl"

namespace tests {

void
support::msg(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

void
support::detail(const char *, ...)
{
}

#if TESTING
tests::eventwaiter<void>::eventwaiter(
    tests::event<void> &_evt,
    std::function<void ()> _action)
    : eventwaiter<void *>(_evt,
                          [_action] (void *) { _action(); }) {}
#endif

struct test {
    const char *name;
    std::function<void (support &)> doit;
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
    testcaseS(c_name, t_name, [doit] (support &) { doit(); }); }

void
testcaseS(const char *c_name,
          const char *t_name,
          std::function<void (support &)> doit) {
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
testcaseCS(const char *c_name,
           const char *t_name,
           std::function<void (controlserver *, clientio)> doit) {
    testcaseS(c_name,
              t_name,
              [doit] (support &) {
                  waitbox<shutdowncode> s;
                  unlink("testcontroller");
                  initpubsub();
                  auto cs(controlserver::build(
                              peername::local(filename("testcontroller"))
                              .fatal("peername testcontroller"),
                              s));
                  assert(cs.issuccess());
                  doit(cs.success(), clientio::CLIENTIO);
                  cs.success()->destroy(clientio::CLIENTIO);
                  deinitpubsub(clientio::CLIENTIO); }); }

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
    for (auto it(components.start()); !it.finished(); it.next()) {
        if (!strcmp(component, "*")) {
            printf("%s:\n", it->name);
        } else if (strcmp(it->name, component)) {
            continue; }
        for (auto it2(it->tests.start()); !it2.finished(); it2.next()) {
            if (!strcmp(t, "*")) {
                printf("        %s:\n", it2->name);
            } else if (strcmp(it2->name, t)) {
                continue; }
            support s;
            it2->doit(s); } } }

} /* End namespace tests */
