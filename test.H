#ifndef TEST_H__
#define TEST_H__

/* Various bits and bobs of use to the self test bits. */

#include <functional>

#include "pubsub.H"

namespace tests {

class support {
public:
    void msg(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    void detail(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
};

/* Test events are a way of controlling the scheduling of events
 * during a test case, making it easier to get races to go the right
 * way.  The idea is that interesting scheduling points call
 * tests::event() with a named event.  In a normal build, those are
 * all no-ops, but in a testing one they go and check whether there
 * are any eventwaiters on the same named event and, if there are,
 * give the waiters a chance to do something interesting. */
/* Caution: destructing an eventwaiter waits for any outstanding
 * invocations of the event to finish, despite not having a clientio
 * token.  The action function must therefore be simple. */
void event(const char *name);
class eventwaiter {
public: const char *name;
public: std::function<void ()> action;
public: int refcount;
public: publisher idle;
public: eventwaiter(const char *, std::function<void ()>);
public: ~eventwaiter();
};

void testcaseS(
    const char *component,
    const char *test,
    std::function<void (support &)> doit);
void testcaseV(
    const char *component,
    const char *test,
    std::function<void ()> doit);

void listcomponents();
void listtests(const char *component);
void runtest(const char *component, const char *test);
}

#endif
