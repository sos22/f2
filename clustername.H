#ifndef CLUSTERNAME_H__
#define CLUSTERNAME_H__

#include "string.H"

class clustername;
template <typename> class maybe;

namespace fields {
class field;
const field &mk(const clustername &); }

class clustername {
    friend const fields::field &fields::mk(const clustername &);
public:  clustername(deserialise1 &);
public:  void serialise(serialise1 &) const;
    /* Limit cluster name size to reduce risk of hitting wireproto
     * message size limits. */
public:  static const unsigned maxsize;
private: ::string value;
private: clustername(const string &s) : value(s) {}
public:  clustername(const quickcheck &);
public:  bool operator==(const clustername &) const;
public:  bool operator!=(const clustername &) const;
public:  static maybe<clustername> mk(const string &); };

template <typename> class parser;
namespace parsers {
const parser<clustername> &__clustername(); }

namespace tests {
void __clustername(); }

#endif /* !CLUSTERNAME_H__ */
