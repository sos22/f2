/* Simple enum type for identifying interfaces. */
#ifndef INTERFACETYPE_H__
#define INTERFACETYPE_H__

class deserialise1;
namespace fields { class field; }
template <typename> class parser;
class quickcheck;
class serialise1;

class interfacetype {
private: unsigned v;
private: explicit interfacetype(unsigned);
public:  interfacetype(quickcheck &);
public:  interfacetype(deserialise1 &);
public:  void serialise(serialise1 &) const;
public:  bool operator==(interfacetype o) const { return v == o.v; }
public:  bool operator!=(interfacetype o) const { return v != o.v; }
public:  const fields::field &field() const;
public:  static const ::parser<interfacetype> &parser();
public:  static const interfacetype storage;
public:  static const interfacetype test;
public:  static const interfacetype test2;
};

namespace fields {
static inline const field &mk(interfacetype o) { return o.field(); } }

#endif /* !INTERFACETYPE_H__ */
