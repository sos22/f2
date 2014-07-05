#include "test.H"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <functional>

#include "list.H"

#include "list.tmpl"

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

static mutex_t eventslock;
static list<eventwaiter *> events;

void
event(const char *name) {
    list<eventwaiter *> l;
    auto token(eventslock.lock());
    for (auto it(events.start()); !it.finished(); it.next()) {
        if (!strcmp(name, (*it)->name)) {
            (*it)->refcount++;
            l.pushtail(*it); } }
    eventslock.unlock(&token);
    for (auto it(l.start()); !it.finished(); it.next()) {
        (*it)->action();
        token = eventslock.lock();
        (*it)->refcount--;
        if ((*it)->refcount == 0) (*it)->idle.publish();
        eventslock.unlock(&token); }
    l.flush(); }

eventwaiter::eventwaiter(
    const char *_name,
    std::function<void ()> _action)
    : name(_name),
      action(_action),
      refcount(0),
      idle() {
    auto token(eventslock.lock());
    events.pushtail(this);
    eventslock.unlock(&token); }

eventwaiter::~eventwaiter() {
    auto token(eventslock.lock());
    bool found = false;
    for (auto it(events.start()); !it.finished(); it.next()) {
        if (*it == this) {
            it.remove();
            found = true;
            break; } }
    eventslock.unlock(&token);
    assert(found);
    subscriber sub;
    subscription ss(sub, idle);
    /* Waiting for the action callbacks without a clientio token isn't
       really ideal.  On the other hand, the action callbacks are
       supposed to be short, and they don't have a clientio token
       either, so it's not completely unreasonable. */
    while (refcount) sub.wait(); }

struct test {
    const char *name;
    std::function<void (support &)> doit;
};

struct testcomponent {
    const char *name;
    list<test> tests;
    ~testcomponent() { tests.flush(); }
};

struct testregistry : public list<testcomponent> {
    ~testregistry() { flush(); } };

static testregistry components;

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
testcaseV(const char *c_name,
          const char *t_name,
          std::function<void ()> doit) {
    testcaseS(c_name, t_name, [doit] (support &) { doit(); }); }

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

template class list<tests::testcomponent>;
template class list<tests::test>;
template class list<tests::eventwaiter *>;
template class std::function<void (tests::support&)>;
template class std::function<void (void)>;
