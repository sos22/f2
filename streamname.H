/* Names for the various output streams produced by a task.  These are
   strings of printable ASCII characters, excluding the '/'
   character. */
#ifndef STREAMNAME_H__
#define STREAMNAME_H__

#include "string.H"

class deserialise1;
namespace fields { class field; }
template <typename> class maybe;
template <typename> class parser;
class quickcheck;
class serialise1;

class streamname {
private: string content;
private: explicit streamname(const string &);
private: bool isvalid() const;
private: void mkrandom(deserialise1 &ds);
public:  static maybe<streamname> mk(const string &);
public:  explicit streamname(deserialise1 &);
public:  explicit streamname(quickcheck);
public:  streamname(_Steal, streamname &);
public:  void serialise(serialise1 &) const;
public:  bool operator<(const streamname &o) const { return content< o.content;}
public:  bool operator<=(const streamname &o) const {return content<=o.content;}
public:  bool operator==(const streamname &o) const {return content==o.content;}
public:  bool operator!=(const streamname &o) const {return content!=o.content;}
public:  bool operator>=(const streamname &o) const {return content>=o.content;}
public:  bool operator>(const streamname &o) const { return content >o.content;}
public:  static const ::parser<streamname> &parser();
public:  unsigned long hash() const;
public:  const fields::field &field() const;
public:  string asfilename() const;
public:  static const ::parser<streamname> &filenameparser(); };

namespace fields {
static inline const field &
mk(const streamname &s) { return s.field(); } }

#endif /* !STREAMNAME_H__ */
