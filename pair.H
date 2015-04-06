#ifndef PAIR_H__
#define PAIR_H__

#include "serialise.H"

namespace fields { class field; }
template <typename> class parser;
namespace parsers {
template <typename a> const parser<a> &defaultparser(); }
class quickcheck;

template <typename a, typename b>
class pair {
private: a fst;
private: b snd;
public:  pair(const a &_fst, const b &_snd)
    : fst(_fst),
      snd(_snd) {}
public:  pair(const pair<a, b> &o)
    : fst(o.fst),
      snd(o.snd) {}
public:  explicit pair(deserialise1 &ds)
    : fst(ds),
      snd(ds) {}
public:  pair(const quickcheck &q);
public:  void serialise(serialise1 &s) const {
    s.push(fst);
    s.push(snd); }
public:  void operator=(const pair<a, b> &o) {
    fst = o.fst;
    snd = o.snd; }
public:  a &first() { return fst; }
public:  const a &first() const { return fst; }
public:  b &second() { return snd; }
public:  const b &second() const { return snd; }
public:  operator a&() { return first(); }
public:  bool operator==(const pair &o) const {
    return o.fst == fst && o.snd == snd; }
public:  bool operator!=(const pair &o) const {
    return o.fst != fst || o.snd != snd; }
    /* Annoyingly, we can't do the symmetrical operator b& alias for
       second, because that's an ambiguous overload when a and b
       happen to be the same types. */
public:  const fields::field &field() const;
public:  static const ::parser<pair<a, b> > &parser(
    const parser<a> & = parsers::defaultparser<a>(),
    const parser<b> & = parsers::defaultparser<b>()); };

namespace fields {
template <typename a, typename b> const field &
mk(const pair<a, b> &x) { return x.field(); } }

template <typename a, typename b> pair<a,b>
mkpair(const a &_a, const b &_b) {
    return pair<a,b>(_a, _b); }

namespace tests { void _pair(); }

#endif /* !PAIR_H__ */
