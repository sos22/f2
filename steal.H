#ifndef STEAL_H__
#define STEAL_H__

/* Tag type indicating that a constructor should steal the content of
 * a buffer rather than copying it.  This is pretty much just a more
 * explicit way of getting rvalue semantics. */
class _Steal {};
extern const _Steal Steal;

#endif /* !STEAL_H__ */
