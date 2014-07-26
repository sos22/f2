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

registrationsecret::registrationsecret(const quickcheck &q) {
    string s;
    do { s = string(q); } while (s.len() > MAX_LEN);
    secret = s; }

registrationsecret::registrationsecret(const registrationsecret &o)
    : secret(o.secret)
{}

bool
registrationsecret::operator==(const registrationsecret &o) const {
    return secret == o.secret; }

maybe<registrationsecret>
registrationsecret::mk(const string &what)
{
    if (what.len() > MAX_LEN) return Nothing;
    else return registrationsecret(what);
}

const fields::field &
fields::mk(const registrationsecret &rs)
{
    return "<registrationsecret:" + fields::mk(rs.secret).escape() + ">";
}

orerror<registrationsecret>
registrationsecret::parse(const char *what) {
    return ("<registrationsecret:" + parsers::strparser + ">")
        .map<registrationsecret>(
            [] (const char *x) { return registrationsecret(x); })
        .match(what); }
