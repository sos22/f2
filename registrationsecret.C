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

void
registrationsecret::operator=(const registrationsecret &o) {
    free(secret);
    secret = strdup(o.secret); }

bool
registrationsecret::operator==(const registrationsecret &o) const {
    return !strcmp(secret, o.secret); }

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
