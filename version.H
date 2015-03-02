#ifndef VERSION_H__
#define VERSION_H__

class deserialise1;
namespace fields { class field; }
class quickcheck;
class serialise1;

class version {
private: unsigned v;
public:  explicit version(deserialise1 &);
public:  explicit version(unsigned _v) : v(_v) {}
public:  void serialise(serialise1 &) const;
public:  static const version current;
    /* Only really useful in unit tests */
public:  static const version invalid;
public:  bool operator<(version o) const { return v < o.v; }
public:  bool operator>(version o) const { return v > o.v; }
public:  bool operator==(version o) const { return v == o.v; }
public:  bool operator!=(version o) const { return v != o.v; }
public:  const fields::field &field() const; };

namespace fields {
static inline const field &mk(version v) { return v.field(); } }

#define CURRENTVERSION (version(1))

#endif /* !VERSION_H__ */
