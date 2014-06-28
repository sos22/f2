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

const fields::field &
fields::mk(const registrationsecret &rs)
{
    return "<registrationsecret:" + fields::mk(rs.secret) + ">";
}
