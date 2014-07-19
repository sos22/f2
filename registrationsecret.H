#ifndef REGISTRATIONSECRET_H__
#define REGISTRATIONSECRET_H__

#include "wireproto.H"

class quickcheck;
class registrationsecret;

namespace fields {
class field;
const field &mk(const registrationsecret &);
}

class registrationsecret {
    friend const fields::field &fields::mk(const registrationsecret &);
    char *secret;
    explicit registrationsecret(const char *_secret);
    WIREPROTO_WRAPPER_TYPE(registrationsecret, const char *);
public:

    registrationsecret(const registrationsecret &);
    ~registrationsecret();
    registrationsecret();
    explicit registrationsecret(const quickcheck &);
    void operator=(const registrationsecret &);
    bool operator==(const registrationsecret &) const;
    static maybe<registrationsecret> mk(const char *what);
    static orerror<registrationsecret> parse(const char *);
};

#endif /* !REGISTRATIONSECRET_H__ */
