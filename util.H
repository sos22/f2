/* Random grab bag of useful functions. */
#ifndef UTIL_H__
#define UTIL_H__

#include <stdlib.h>

#define ARRAYSIZE(x) (sizeof(x)/sizeof(x[0]))

namespace fields { class field; }
template <typename t> class orerror;

/* XXX convert everything over to parseint */
orerror<long> parselong(const char *);

template <typename t> t atomicloaddec(t &what);
template <typename t> void atomicinc(t &what);
template <typename t> t atomicswap(t &what, t);

template <typename t> t
min(const t &a) { return a; }
template <typename t, typename ... args> t
min(const t &a, args ... params) {
    auto r(min(params...));
    if (r < a) return r;
    else return a; }

template <typename t> t
max(const t &a) { return a; }
template <typename t, typename ... args> t
max(const t &a, args ... params) {
    auto r(max(params...));
    if (r > a) return r;
    else return a; }

#define CONCAT(a, b) a##b
#define CONCAT2(a, b) CONCAT(a, b)

class futex;

/* Class for things which are accessed without locks. Tries to give
 * Helgrind some useful false positive-suppressing hints, as well as
 * doing the actual op. Only makes sense for primitive types. */
template <typename t> class racey {
    friend class futex;
private: t content;
private: void operator=(const racey &) = delete;
public:  racey(t x) : content(x) {}
    /* Simple atomic load and store. */
public:  t load() const;
public:  void store(t);
    /* Like load and store, but with extra barriers. loads and
     * acquires on x86 are always acquire and release barriers, so no
     * real-world difference, but it's sometimes useful
     * documentation. */
public:  t loadacquire() const { return load(); }
public:  void storerelease(t x) { store(x); }
    /* Atomically fetch something and add @delta to it.  Returns the
     * pre-increase version. */
public:  t fetchadd(t delta);
    /* Atomically compare and swap, returning the old value. */
public:  t compareswap(t old, t _new);
public:  const fields::field &field() const; };

/* Cast to 1 rather than NULL to avoid stupid compiler crap. */
#define containerof(thing, type, field)                                 \
    ({  type *__res = (type *)((unsigned long)thing + 1-                \
                               (unsigned long)&((type *)1)->field);     \
        (void)(&__res->field == (thing));                               \
        __res;                                                          \
    })

void mb();

namespace proto { namespace eq { class eventid; } };
template <> class racey<proto::eq::eventid>;

#endif /* !UTIL_H__ */
