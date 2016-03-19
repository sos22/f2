#ifndef FUTEX_H__
#define FUTEX_H__

#include "util.H"

class clientio;
namespace fields { class field; }
template <typename t> class maybe;
class timestamp;

class futex {
private: racey<unsigned> inner;
public:  futex() : inner(0) {}
public:  explicit futex(unsigned i) : inner(i) {}
    /* Change the futex and wake everyone waiting on it. */
public:  void set(unsigned);
    /* Increase the futex, wake everyone waiting on it, and return the
     * old value. */
public:  unsigned fetchadd(unsigned);
    /* Get the current value of the futex. */
public:  unsigned poll() const;
    /* Wait while the futex is a given value, with a timeout,
     * returning the observed value or Nothing. */
public:  maybe<unsigned> wait(clientio, unsigned, maybe<timestamp>) const;
    /* Wait while the futex is a given value, returning the observed
     * different value. */
public:  unsigned wait(clientio, unsigned) const;
public:  const fields::field &field() const; };

#endif /* !FUTEX_H__ */
