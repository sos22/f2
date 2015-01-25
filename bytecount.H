#ifndef BYTECOUNT_H__
#define BYTECOUNT_H__

#include "fields.H"
#include "serialise.H"

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

public:  bool operator<(bytecount o) const { return b < o.b; }
public:  bool operator<=(bytecount o) const { return b <= o.b; }
public:  bool operator==(bytecount o) const { return b == o.b; }
public:  bool operator!=(bytecount o) const { return b != o.b; }
public:  bool operator>=(bytecount o) const { return b >= o.b; }
public:  bool operator>(bytecount o) const { return b > o.b; }

public:  const fields::field &field() const { return fields::mk(b) + "B"; }
};

static inline bytecount
operator"" _B(unsigned long long x) { return bytecount::bytes(x); }

template <typename> class parser;
namespace parsers {
const parser< ::bytecount> &_bytecount(); }

#endif /* !BYTECOUNT_H__ */
