/* Simple type used to represent strings, with a bit of basic algebra.
 * Not particular efficient. */
#ifndef STRING_H__
#define STRING_H__

#include <stddef.h>

class _Steal;
class deserialise1;
template <typename> class maybe;
template <typename> class parser;
class quickcheck;
class serialise1;
class string;

namespace fields {
class strfield;
const strfield &mk(const string &);
};

class string {
    friend const fields::strfield &fields::mk(const string &);
private: char *content; /* owned malloc()ed nul-terminated */
public:  string();
    /* Construct a string by stealing a pointer from the caller.  The
       pointer must be malloc()ed and nul-terminated.  The string will
       take responsibility for eventually releasing it. */
public:  static string steal(char *&);
public:  string(_Steal, string &);
public:  string(const char *);
public:  string(const string &);
public:  string(string &&);
public:  explicit string(deserialise1 &);
public:  void operator=(const string &);
public:  string operator+(const string &) const;
public:  void operator+=(const char *);
public:  bool operator<(const string &) const;
public:  bool operator<=(const string &) const;
public:  bool operator==(const string &) const;
public:  bool operator!=(const string &) const;
public:  bool operator>=(const string &) const;
public:  bool operator>(const string &) const;
public:  size_t len() const;
public:  ~string();

public:  void serialise(serialise1 &) const;

    /* Truncate the string in place to be precisely sz characters
     * (which must be less than or equal to the current size). */
public:  void truncate(size_t sz);

    /* Return the string pointer embedded in the string.  Remains
       valid as long as this does. */
    /* XXX Really rather skanky XXX */
public:  const char *c_str() const;

    /* If @p is a prefix of *this, return the bit of *this after the
     * prefix.  Otherwise, return NULL. */
public:  maybe<string> stripprefix(const string &p) const;

public:  const fields::strfield &field() const { return fields::mk(*this); }

public:  static const ::parser<string> &parser();

public:  unsigned long hash() const; };

namespace tests { void _string(void); }

#endif /* !STRING_H__ */
