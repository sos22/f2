/* A header which gets included automatically into every file. */
#ifndef FUNDAMENTALS_H__
#define FUNDAMENTALS_H__

/* Tag type indicating that a constructor should steal the content of
 * a buffer rather than copying it.  This is pretty much just a more
 * explicit way of getting rvalue semantics. */
class _Steal {};
static const _Steal Steal;

/* x != y should always be !(x == y) */
template <typename a, typename b> bool
operator != (const a &_a, const b &_b) { return !(_a == _b); }
/* And some similar rules about < and <= */
template <typename a, typename b> bool
operator <(const a &_a, const b &_b) { return !(_a >= _b); }
template <typename a, typename b> bool
operator <=(const a &_a, const b &_b) { return !(_a > _b); }

#endif /* !FUNDAMENTALS_H__ */
