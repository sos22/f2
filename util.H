/* Random grab bag of useful functions. */
#ifndef UTIL_H__
#define UTIL_H__

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

template <typename t> class orerror;

orerror<long> parselong(const char *);

#endif /* !UTIL_H__ */
