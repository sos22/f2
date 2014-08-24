/* Representation of time according to a wall clock.  These reflect
   user-initiated changes in the time of day, so differences between
   them are almost meaningless. */
#ifndef WALLTIME_H__
#define WALLTIME_H__

#include "fields.H"
#include "wireproto.H"

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
private: unsigned long v; /* nanoseconds since epoch */
private: walltime() = delete;
public:  walltime(quickcheck);
public:  bool operator==(walltime) const;
public:  static walltime now();
WIREPROTO_WRAPPER_TYPE(walltime, unsigned long, private);
};

#endif /* !WALLTIME_H__ */
