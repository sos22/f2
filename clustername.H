#ifndef CLUSTERNAME_H__
#define CLUSTERNAME_H__

#include "string.H"

class clustername;
namespace fields { class field; }
template <typename> class maybe;


class clustername {
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
public:  static maybe<clustername> mk(const string &);
public:  const fields::field &field() const; };

template <typename> class parser;
namespace parsers {
const parser<clustername> &__clustername(); }

#endif /* !CLUSTERNAME_H__ */
