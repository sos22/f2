/* Simple enum type for tracking what kind of thing a particular
 * network agent is: master, storage slave, or compute slave. */
#ifndef ACTORTYPE_H__
#define ACTORTYPE_H__

#include "wireproto.H"

class actortype;
class quickcheck;

namespace fields {
class field;
const field &mk(actortype);
}

template <typename> class parser;
namespace parsers {
const parser<actortype> &_actortype();
}

class actortype {
private: int val;
public:  static const actortype master;
public:  static const actortype storageslave;
public:  static const actortype cli;
public:  static const actortype test;
public:  actortype(quickcheck);
public:  bool operator==(const actortype &o) const { return val == o.val; }
WIREPROTO_WRAPPER_TYPE(actortype, int, private);
};

namespace tests {
void _actortype();
}

#endif /* !ACTORTYPE_H__ */
