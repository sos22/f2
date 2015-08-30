#ifndef EITHER_H__
#define EITHER_H__

#include <assert.h>

#include <utility>

#include "quickcheck.H"

class deserialise1;
class serialise1;

/* Tag types used only to drive constructor overloading in the right
 * direction. */
class Left {};
class Right {};

template <typename a, typename b>
class either {
public:  typedef either<a,b> type;
public:  typedef a type_left;
public:  typedef b type_right;
private: bool isleft_;
private: union {
    a _left;
    b _right; };
private: either() {};
public:  either(quickcheck &q);
public:  either(const either &o);
public:  either(deserialise1 &ds);
public:  void serialise(serialise1 &s) const;
public:  template <typename ... args> either(Left, args &&...params);
public:  template <typename ... args> either(Right, args &&...params);
public:  bool isleft() const;
public:  bool isright() const;
public:  const a &left() const;
public:  a &left();
public:  const b &right() const;
public:  b &right();
public:  void operator=(const either &o);
public:  template <typename ... args> void mkleft(args &&...params);
public:  template <typename ... args> void mkright(args &&...params);
public:  bool operator==(const either &o) const;
public:  bool operator!=(const either &o) const;
public:  ~either(); };

/* Work around C++'s odd void handling. */
class Void {};
template <typename a> class either<void, a> : public either<Void, a> {
public: using either<Void, a>::either;
};
template <typename a> class either<a, void> : public either<a, Void> {
public: using either<a, Void>::either; };

template <typename r, typename a> either <a, r> left(const a &what);

template <typename l, typename a> either <l, a> right(const a &what);

#endif /* !EITHER_H__ */
