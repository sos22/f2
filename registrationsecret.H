#ifndef REGISTRATIONSECRET_H__
#define REGISTRATIONSECRET_H__

#include "wireproto.H"

class registrationsecret;

namespace fields {
class field;
const field &mk(const registrationsecret &);
}

class registrationsecret {
    friend const fields::field &fields::mk(const registrationsecret &);
    char *secret;
    explicit registrationsecret(const char *_secret);
public:
    WIREPROTO_TYPE(registrationsecret);

    registrationsecret(const registrationsecret &);
    ~registrationsecret();
    registrationsecret();;
    void operator=(const registrationsecret &);
    bool operator==(const registrationsecret &) const;
    static registrationsecret mk(const char *what);
};

#endif /* !REGISTRATIONSECRET_H__ */
