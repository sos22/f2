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
public:  explicit interfacetype(quickcheck &);
public:  explicit interfacetype(deserialise1 &);
public:  void serialise(serialise1 &) const;
public:  bool operator==(interfacetype o) const { return v == o.v; }
public:  bool operator!=(interfacetype o) const { return v != o.v; }
public:  const fields::field &field() const;
public:  static const ::parser<interfacetype> &parser();
public:  static const interfacetype meta;
public:  static const interfacetype storage;
public:  static const interfacetype eq; /* Event queues */
public:  static const interfacetype compute;
public:  static const interfacetype filesystem;
public:  static const interfacetype test;
public:  static const interfacetype test2;

    /* Little bit of a hack: interfaces are normally unordered, so
     * don't have a < operator, but we want to make sure that if a
     * beacon server advertises two interfaces it always does them in
     * the same order.  Expose ord rather than operator < so that we
     * can do that without risking odd behaviour elsewhere. */
public:  bool ord(interfacetype o) const { return v < o.v; }
};

namespace fields {
static inline const field &mk(interfacetype o) { return o.field(); } }

#endif /* !INTERFACETYPE_H__ */
