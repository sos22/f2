/* Random grab bag of useful functions. */
#ifndef UTIL_H__
#define UTIL_H__

#include <stdlib.h>

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

template <typename t> class orerror;

orerror<long> parselong(const char *);

template <typename t> t loadacquire(const t &what);
template <typename t> void storerelease(t *where, t what);

#endif /* !UTIL_H__ */
