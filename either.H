#ifndef EITHER_H__
#define EITHER_H__

#include <assert.h>

template <typename a, typename b>
class either {
private: bool isleft_;
private: union {
    a _left;
    b _right; };
private: either() {};
public:  either(const either &o)
    : isleft_(o.isleft_) {
    if (isleft()) new (&_left) a(o.left());
    else new (&_right) b(o.right()); }
public:  static either left(const a &what) {
    either<a, b> res;
    res.isleft_ = true;
    new (&res._left) a(what);
    return res; }
public:  either(const b &);
public:  static either right(const b &what) {
    either<a, b> res;
    res.isleft_ = false;
    new (&res._right) b(what);
    return res; }
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
public:  ~either() {
    if (isleft_) _left.~a();
    else _right.~b(); }
};

template <typename r, typename a> either <a, r> left(const a &what) {
    return either<a, r>::left(what); }

template <typename l, typename a> either <l, a> right(const a &what) {
    return either<l, a>::right(what); }

namespace tests {
void either();
}

#endif /* !EITHER_H__ */
