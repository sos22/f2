/* Simple type used to represent strings, with a bit of basic algebra.
 * Not particular efficient. */
#ifndef STRING_H__
#define STRING_H__

#include <stddef.h>

#include "wireproto.H"

class quickcheck;
class string;

namespace fields {
class strfield;
const strfield &mk(const string &);
};

class string {
    friend const fields::strfield &fields::mk(const string &);
private: const char *content; /* owned malloc()ed nul-terminated */
public:  string();
    /* Construct a string by stealing a pointer from the caller.  The
       pointer must be malloc()ed and nul-terminated.  The string will
       take responsibility for eventually releasing it. */
public:  static string steal(char *&);
public:  string(const char *);
public:  string(const string &);
public:  string(string &&);
public:  void operator=(const string &);
public:  string operator+(const string &) const;
public:  bool operator==(const string &) const;
public:  bool operator!=(const string &) const;
public:  bool operator<(const string &) const;
public:  bool operator>(const string &) const;
public:  size_t len() const;
public:  ~string();

    /* Return the string pointer embedded in the string.  Remains
       valid as long as this does. */
    /* XXX Really rather skanky XXX */
public:  const char *c_str() const;
};

#endif /* !STRING_H__ */
