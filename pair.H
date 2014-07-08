#ifndef PAIR_H__
#define PAIR_H__

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
public:  void operator=(const pair<a, b> &o) {
    fst = o.fst;
    snd = o.snd; }
public:  a &first() { return fst; }
public:  const a &first() const { return fst; }
public:  b &second() { return snd; }
public:  const b &second() const { return snd; }
public:  operator a&() { return first(); }
public:  operator const a&() const { return first(); }
public:  operator const b&() const { return second(); }
};

template <typename a, typename b> pair<a,b>
mkpair(const a &_a, const b &_b) {
    return pair<a,b>(_a, _b); }

#endif /* !PAIR_H__ */