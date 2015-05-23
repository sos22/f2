#ifndef PAIR_H__
#define PAIR_H__

#include "serialise.H"
#include "steal.H"

namespace fields { class field; }
template <typename> class parser;
namespace parsers {
template <typename a> const parser<a> &defaultparser(); }
class quickcheck;

template <typename a, typename b>
class pair {
private: a fst;
private: b snd;
    /* I'd really like to do the steal constructors as a single thing
     * which steals both arguments, but C++ overloading doesn't work
     * in quite the right way to do that with the builtin types, so
     * use a bunch of constructors which steal one or other of the
     * arguments instead. */
public:  pair(const a &_fst, const b &_snd);
public:  pair(_Steal s, a &_fst, const b &_snd);
public:  pair(const a &_fst, _Steal s, b &_snd);
public:  pair(_Steal s1, a &_fst, _Steal s2, b &_snd);
public:  pair(const pair<a, b> &o);
public:  pair(_Steal s, pair<a, b> &o);
public:  pair(pair<a, b> &o, _Steal s);
public:  pair(_Steal s1, pair<a, b> &o, _Steal s2);
public:  pair(const quickcheck &q);
public:  explicit pair(deserialise1 &ds);
public:  void serialise(serialise1 &s) const;
public:  void operator=(const pair<a, b> &o);
public:  a &first();
public:  const a &first() const;
public:  b &second();
public:  const b &second() const;
public:  bool operator==(const pair &o) const;
public:  bool operator!=(const pair &o) const;
public:  const fields::field &field() const;
public:  static const ::parser<pair<a, b> > &parser(
    const parser<a> & = parsers::defaultparser<a>(),
    const parser<b> & = parsers::defaultparser<b>()); };

template <typename a, typename b> pair<a,b> mkpair(const a &_a, const b &_b);

#endif /* !PAIR_H__ */
