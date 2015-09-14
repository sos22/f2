/* Representation of time according to a wall clock.  These reflect
   user-initiated changes in the time of day, so differences between
   them are almost meaningless. */
#ifndef WALLTIME_H__
#define WALLTIME_H__

#include "fields.H"

template <typename> class parser;
class quickcheck;
class walltime;
class walltimeparser;

namespace fields {
class field;
const field &mk(walltime);
}

namespace tests {
void _walltime();
}

namespace parsers {
const parser<walltime> &_walltime();
}

class walltime {
    friend const fields::field &fields::mk(walltime);
    friend void tests::_walltime();
    friend class walltimeparser;
private: long v; /* nanoseconds since epoch */
private: walltime() = delete;
private: explicit walltime(long _v) : v(_v) {}
public:  static walltime __testmk(long v) { return walltime(v); }
public:  explicit walltime(deserialise1 &);
public:  void serialise(serialise1 &) const;
public:  bool operator==(walltime) const;
public:  static walltime now();
};

#endif /* !WALLTIME_H__ */
