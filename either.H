#ifndef EITHER_H__
#define EITHER_H__

#include <assert.h>

template <typename a, typename b>
class either {
private: void operator=(const either &) = delete;
private: bool isleft_;
private: char content[sizeof(a) > sizeof(b) ? sizeof(a) : sizeof(b)];
private: either() {};
public:  either(const either &o)
    : isleft_(o.isleft_) {
    if (isleft()) new (content) a(o.left());
    else new (content) b(o.right()); }
public:  static either left(const a &what) {
    either<a, b> res;
    res.isleft_ = true;
    new (res.content) a(what);
    return res; }
public:  either(const b &);
public:  static either right(const b &what) {
    either<a, b> res;
    res.isleft_ = false;
    new (res.content) b(what);
    return res; }
public:  bool isleft() const { return isleft_; }
public:  bool isright() const { return !isleft_; }
public:  const a &left() const {
    assert(isleft());
    return *(const a *)content; }
public:  a &left() {
    assert(isleft());
    return *(a *)content; }
public:  const b &right() const {
    assert(isright());
    return *(const b *)content; }
public:  b &right() {
    assert(isright());
    return *(b *)content; }
public:  void mkright(const b &);
public:  void mkleft(const a &);
public:  ~either() {
    if (isleft_) left().~a();
    else right().~b(); }
};

template <typename r, typename a> either <a, r> left(const a &what) {
    return either<a, r>::left(what); }

template <typename l, typename a> either <l, a> right(const a &what) {
    return either<l, a>::right(what); }

namespace tests {
void either();
}

#endif /* !EITHER_H__ */
