#include "registrationsecret.H"

#include <stdlib.h>
#include <string.h>

#include "fields.H"

registrationsecret::registrationsecret(const char *_secret)
    : secret(strdup(_secret))
{}

registrationsecret::registrationsecret(const registrationsecret &o)
    : secret(strdup(o.secret))
{}

registrationsecret::~registrationsecret()
{
    free(secret);
}

registrationsecret
registrationsecret::mk(const char *what)
{
    return registrationsecret(what);
}

class registrationsecretfield : fields::field {
    registrationsecret content;
    registrationsecretfield(const registrationsecret &_content)
        : content(_content)
        {}
public:
    static const field &mk(const registrationsecret &rs)
        {
            return *new registrationsecretfield(rs);
        }
    void fmt(fields::fieldbuf &buf) const
        {
            buf.push("<registrationsecret:");
            buf.push(content.secret);
            buf.push(">");
        }
};
const fields::field &
fields::mk(const registrationsecret &rs)
{
    return registrationsecretfield::mk(rs);
}
