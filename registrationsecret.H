#ifndef REGISTRATIONSECRET_H__
#define REGISTRATIONSECRET_H__

#include "string.H"
#include "wireproto.H"

class quickcheck;
class registrationsecret;

namespace fields {
class field;
const field &mk(const registrationsecret &);
}

class registrationsecret {
    friend const fields::field &fields::mk(const registrationsecret &);
    string secret;
    WIREPROTO_WRAPPER_TYPE(registrationsecret, string, private);
public:

    registrationsecret(const registrationsecret &);
    registrationsecret();
    explicit registrationsecret(const quickcheck &);
    bool operator==(const registrationsecret &) const;
    static maybe<registrationsecret> mk(const string &);
};

template <typename> class parser;
namespace parsers {
const parser<registrationsecret> &_registrationsecret();
}

namespace tests {
void _registrationsecret();
}

#endif /* !REGISTRATIONSECRET_H__ */
