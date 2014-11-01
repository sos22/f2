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

/* A variant on the same theme: a structured way for bits of code to
 * call back into unit tests for e.g. fault injection.  The hookpoint
 * is the thing which the thing under test calls into, and the hook is
 * the thing which the test harness inserts into it.  Each hookpoint
 * defines a return type (possibly void) and some arguments (possibly
 * an empty list).  The thing under test provides the arguments, which
 * are passed to the test harness hook, and the hook provides the
 * return value, which is passed back to the thing under test.
 * Multiple threads can call into the same hook at once (if several
 * threads reach the same hookpoint at once).  The hook and hookpoint
 * destructors both wait for any extant invocations to finish before
 * releasing the hook.  Each hookpoint can have at most one hook
 * active at any time; attaching several hooks to the same hookpoint
 * causes an abort. */
template <typename rettype, typename ... args> class hook;

template <typename rettype, typename ... args> class hookpoint;
template <typename ... args> class hookpoint<void, args...>;

template <typename rettype, typename ... args> class hookpoint {
    friend class hook<rettype, args...>;
private: hookpoint() = delete;
private: hookpoint(const hookpoint &) = delete;
private: void operator=(const hookpoint &) = delete;
private: mutable mutex_t mux;
private: maybe<hook<rettype, args...>  *> hooked;
    /* Number of invocations of the hook curently ongoing. */
private: mutable int refcount;
    /* Notified when refcount reaches zero. */
private: mutable cond_t idle;
    /* Put this last so that we can have a generic implementation of
     * the destructor which doesn't depend on the types. */
private: std::function<rettype (args &&...)> const dflt;
public:  explicit hookpoint(const std::function<rettype (args &&...)> &);
public:  rettype operator()(args && ... params) const {
    if (hooked == Nothing) return dflt(std::forward<args>(params)...);
    else return slowpath(std::forward<args>(params)...); }
private: rettype slowpath(args && ... params) const;
public:  ~hookpoint(); };

template <typename rettype, typename ... args> class hook {
    friend class hookpoint<rettype, args...>;
private: hook() = delete;
private: hook(const hook &) = delete;
private: void operator=(const hook &) = delete;
private: std::function<rettype (args &&...)> impl;
private: hookpoint<rettype, args...> *const owner;
public:  hook(hookpoint<rettype, args...> &,
              const std::function<rettype (args &&...)> &);
public:  ~hook(); };

/* Need special cases for void rettype, because C++ is stupid like
 * that. */
template <typename ... args> class hookpoint<void, args...> {
private: hookpoint() = delete;
private: hookpoint(const hookpoint &) = delete;
private: void operator=(const hookpoint &) = delete;
private: mutable mutex_t mux;
private: maybe<hook<void, args...>  *> hooked;
private: mutable int refcount;
private: mutable cond_t idle;
private: std::function<void (args &&...)> const dflt;
public:  explicit hookpoint(const std::function<void (args &&...)> &);
public:  void operator()(args && ... params) const {
    if (hooked == Nothing) dflt(std::forward<args>(params)...);
    else slowpath(std::forward<args>(params)...); }
private: void slowpath(args && ... params) const;
public:  ~hookpoint();
    /* Only implemented if args is an empty list.  Would be private if
     * I could figure out how to make hookpoint<void> friends with all
     * the other varieties of hookpoint. */
public:  void set(hook<void, args...> *);
public:  void wait(); };


/* Different test cases need different arguments, and for some reason
   overloading doesn't work properly with lambdas, so have a bunch of
   different functions for registering cases. */
void testcaseV(
    const char *component,
    const char *test,
    std::function<void ()> doit);
void testcaseIO(
    const char *component,
    const char *test,
    std::function<void (clientio)> doit);
void testcaseS(
    const char *component,
    const char *test,
    std::function<void (support &)> doit);
void testcaseCS(
    const char *component,
    const char *test,
    std::function<void (controlserver *, clientio)> doit);

void listcomponents();
void listtests(const char *component);
void runtest(const char *component, const char *test);


void rpctest2();
}

#endif
