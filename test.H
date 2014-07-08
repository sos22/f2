#ifndef TEST_H__
#define TEST_H__

/* Various bits and bobs of use to the self test bits. */

#include <functional>

#include "pubsub.H"

class controlserver;

namespace tests {

class support {
public:
    void msg(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    void detail(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
};

/* Test events are a way of interfering with a program's operation
 * while it runs, making it easier to explore races or error
 * conditions.  The idea is that the program to be tested defines some
 * event structures and arranges to call trigger() at interesting
 * points.  The harness must arrange to construct an eventlistener for
 * the relevant event before that happens, and it will then get the
 * chance to mess round with either the state, or just screw with the
 * scheduling. */
/* Caution: destructing an eventwaiter waits for any outstanding
 * invocations of the event to finish, despite not having a clientio
 * token.  The action function must therefore be simple. */
#if TESTING
template <typename t> class eventwaiter;
template <typename t> class event {
public: mutex_t mux;
public: list<void *> listeners;
public: void trigger(t);
};
template <typename t> class eventwaiter {
private: event<t> &evt;
public:  std::function<void (t)> action;
public:  int refcount;
public:  publisher idle;
public:  eventwaiter(event<t> &, std::function<void (t)>);
public:  ~eventwaiter();
};
template <> class eventwaiter<void> : private eventwaiter<void *> {
public: eventwaiter(event<void> &, std::function<void ()>);
};
#else
template <typename t> class event {
public: void trigger(t) {};
};
#endif
template <> class event<void> : public event<void *> {
    friend class voideventwaiter;
public: void trigger() { event<void *>::trigger(NULL); }
};

/* Different test cases need different arguments, and for some reason
   overloading doesn't work properly with lambdas, so have a bunch of
   different functions for registering cases. */
void testcaseV(
    const char *component,
    const char *test,
    std::function<void ()> doit);
void testcaseS(
    const char *component,
    const char *test,
    std::function<void (support &)> doit);
void testcaseCS(
    const char *component,
    const char *test,
    std::function<void (controlserver *)> doit);

void listcomponents();
void listtests(const char *component);
void runtest(const char *component, const char *test);
}

#endif
