/* Random grab bag of useful functions. */
#ifndef UTIL_H__
#define UTIL_H__

#include <stdlib.h>

#define ARRAYSIZE(x) (sizeof(x)/sizeof(x[0]))

template <typename t> class orerror;

/* XXX convert everything over to parseint */
orerror<long> parselong(const char *);

template <typename t> t loadacquire(const t &what);
template <typename t> void storerelease(t *where, t what);
template <typename t> t atomicload(const t &what);
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

/* Cast to 1 rather than NULL to avoid stupid compiler crap. */
#define containerof(thing, type, field)                                 \
    ({  type *__res = (type *)((unsigned long)thing + 1-                \
                               (unsigned long)&((type *)1)->field);     \
        (void)(&__res->field == (thing));                               \
        __res;                                                          \
    })

void mb();

#endif /* !UTIL_H__ */
