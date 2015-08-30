/* A header which gets included automatically into every file. */
#ifndef FUNDAMENTALS_H__
#define FUNDAMENTALS_H__

template <typename a, typename b> bool
operator != (const a &_a, const b &_b) { return !(_a == _b); }

#endif /* !FUNDAMENTALS_H__ */
