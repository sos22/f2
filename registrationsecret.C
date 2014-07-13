#include "registrationsecret.H"

#include <stdlib.h>
#include <string.h>
#include <functional>

#include "pair.H"
#include "fields.H"
#include "parsers.H"
#include "quickcheck.H"
#include "tmpheap.H"

#include "parsers.tmpl"

/* Limit length to avoid problems with wire protocol message size
 * limits. */
#define MAX_LEN 1000

registrationsecret::registrationsecret(const char *_secret)
    : secret(strdup(_secret))
{}

registrationsecret::registrationsecret(const quickcheck &q) {
    const char *s;
    do { s = q; } while (strlen(s) > MAX_LEN);
    secret = strdup(s); }

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

maybe<registrationsecret>
registrationsecret::mk(const char *what)
{
    if (strlen(what) > MAX_LEN) return Nothing;
    else return registrationsecret(what);
}

const fields::field &
fields::mk(const registrationsecret &rs)
{
    return "<registrationsecret:" + fields::mk(rs.secret).escape() + ">";
}

orerror<registrationsecret>
registrationsecret::parse(const char *what) {
    return ("<registrationsecret:" + strparser + ">")
        .map<registrationsecret>(
            [] (const char *x) { return registrationsecret(x); })
        .match(what); }
