/* Special type for non-NULL pointers.  Basically the useful bits of
 * references with all the stupidity. */
#ifndef NNP_H__
#define NNP_H__

#include "maybe.H"

template <typename t> class nnp {
private: nnp() = delete;
private: t *inner;
private: nnp(t *_inner) : inner(_inner) {}
public:  nnp(t &_inner) : inner(&_inner) {}
public:  nnp(const nnp<t> &o) : inner(o.inner) {}
public:  static maybe<nnp<t> > mk(t *x) {
    if (x == NULL) return Nothing;
    else return nnp(x); }
public:  bool operator==(const nnp &o) const { return inner == o.inner; }
public:  bool operator!=(const nnp &o) const { return inner != o.inner; }
public:  t *operator->() const { return inner; }
public:  t &operator *() const { return *inner; }
public:  operator t*() const { return inner; } };

template <typename t> nnp<t> _nnp(t &x) { return nnp<t>(x); }

#endif /* !NNP_H__ */
