#ifndef BYTECOUNT_H__
#define BYTECOUNT_H__

#include "fields.H"
#include "maybe.H"
#include "serialise.H"

template <typename> class parser;

class bytecount {
public:  unsigned long b;
private: explicit bytecount(unsigned long _b) : b(_b) {}

public:  explicit bytecount(deserialise1 &ds) : b(ds) {}
public:  void serialise(serialise1 &s) const { s.push(b); }

public:  static bytecount bytes(unsigned long x) { return bytecount(x); }
public:  static bytecount kilobytes(unsigned long x) { return bytes(x * 1000); }
public:  static bytecount megabytes(unsigned long x) {
    return bytes(x * 1000000); }
public:  static bytecount kibibytes(unsigned long x) { return bytes(x * 1024); }
public:  static bytecount mebibytes(unsigned long x) {
    return bytes(x * 1048576); }

public:  bool operator==(bytecount o) const { return b == o.b; }
public:  bool operator>=(bytecount o) const { return b >= o.b; }
public:  bool operator>(bytecount o) const { return b > o.b; }

public:  maybe<bytecount> operator-(bytecount o) const {
    if (b >= o.b) return bytecount(b - o.b);
    else return Nothing; }
public:  bytecount operator+(bytecount o) const {
    assert(b + o.b >= b);
    return bytecount(b + o.b); }
public:  bytecount operator/(unsigned long o) const {
    return bytecount(b / o); }
public:  bytecount operator *(unsigned long o) const {
    if (o == 0) return bytes(0);
    unsigned long n = b * o;
    assert(n / o == b);
    return bytes(n); }

public:  const fields::field &field() const { return fields::mk(b) + "B"; }
public:  static const ::parser<bytecount> &parser();
public:  template <typename t> static bytecount size(const t &) {
    return bytecount(sizeof(t)); }
};

template <typename t> static inline t *
operator+(t *a, bytecount b) { return a + b.b; }

static inline void *
operator+(void *a, bytecount b) { return (void *)((unsigned long)a + b.b); }

template <typename t> static inline const t *
operator+(const t *a, bytecount b) { return a + b.b; }

static inline const void *
operator+(const void *a, bytecount b) {
    return (const void *)((unsigned long)a + b.b); }

static inline bytecount
operator"" _B(unsigned long long x) { return bytecount::bytes(x); }
static inline bytecount
operator"" _MB(unsigned long long x) { return bytecount::megabytes(x); }
static inline bytecount
operator"" _MiB(unsigned long long x) { return bytecount::mebibytes(x); }

#endif /* !BYTECOUNT_H__ */
