#ifndef REGISTRATIONSECRET_H__
#define REGISTRATIONSECRET_H__

#include "wireproto.H"

class registrationsecret;

namespace fields {
class field;
const field &mk(const registrationsecret &);
}

class registrationsecret {
    friend class registrationsecretfield;
    char *secret;
    explicit registrationsecret(const char *_secret);
    registrationsecret() = delete;
    void operator=(const registrationsecret &) = delete;
public:
    WIREPROTO_TYPE(registrationsecret);

    registrationsecret(const registrationsecret &);
    ~registrationsecret();
    static registrationsecret mk(const char *what);
};

#endif /* !REGISTRATIONSECRET_H__ */
