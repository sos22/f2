/* Random grab bag of useful functions. */
#ifndef UTIL_H__
#define UTIL_H__

#include <sys/types.h>

template <typename t> class orerror;

orerror<long> parselong(const char *);

#endif /* !UTIL_H__ */
