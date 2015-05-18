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
public:  either(quickcheck q)
    : isleft_(q) {
    if (isleft_) new (&_left) a(q);
    else new (&_right) a(q); }
public:  either(const either &o)
    : isleft_(o.isleft_) {
    if (isleft()) new (&_left) a(o.left());
    else new (&_right) b(o.right()); }
public:  either(deserialise1 &ds);
public:  void serialise(serialise1 &s) const;
public:  template <typename ... args> either(Left, args &&...params)
    : isleft_(true) {
    new (&_left) a(std::forward<args>(params)...); }
public:  template <typename ... args> either(Right, args &&...params)
    : isleft_(false) {
    new (&_right) b(std::forward<args>(params)...); }
public:  bool isleft() const { return isleft_; }
public:  bool isright() const { return !isleft_; }
public:  const a &left() const {
    assert(isleft());
    return _left; }
public:  a &left() {
    assert(isleft());
    return _left; }
public:  const b &right() const {
    assert(isright());
    return _right; }
public:  b &right() {
    assert(isright());
    return _right; }
public: void operator=(const either &o) {
    if (isleft_) {
        if (o.isleft_) _left = o._left;
        else {
            _left.~a();
            new (&_right) b(o._right);
            isleft_ = false; } }
    else {
        if (o.isleft_) {
            _right.~b();
            new (&_left) a(o._left);
            isleft_ = true; }
        else _right = o._right; } }
public:  template <typename ... args> void mkleft(args &&...params) {
    if (isleft_) _left.~a();
    else _right.~b();
    isleft_ = true;
    new (&_left) a(std::forward<args>(params)...); }
public:  template <typename ... args> void mkright(args &&...params) {
    if (isleft_) _left.~a();
    else _right.~b();
    isleft_ = false;
    new (&_right) b(std::forward<args>(params)...); }
public: bool operator==(const either &o) const {
    if (isleft_ != o.isleft_) return false;
    if (isleft_) return _left == o._left;
    else return _right == o._right; }
public: bool operator!=(const either &o) const {
    if (isleft_ != o.isleft_) return true;
    if (isleft_) return _left != o._left;
    else return _right != o._right; }
public:  ~either() {
    if (isleft_) _left.~a();
    else _right.~b(); }
};

template <typename r, typename a> either <a, r> left(const a &what) {
    return either<a, r>(Left(), what); }

template <typename l, typename a> either <l, a> right(const a &what) {
    return either<l, a>(Right(), what); }

#endif /* !EITHER_H__ */
