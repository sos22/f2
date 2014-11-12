/* This really doesn't deserve to be called a digest */
#include "digest.H"

#include "either.H"
#include "fields.H"
#include "logging.H"
#include "parsers.H"
#include "serialise.H"
#include "string.H"

#include "either.tmpl"
#include "parsers.tmpl"

digest::digest(const fields::field &what)
{
    logmsg(loglevel::error,
           fields::mk("need to implement a proper MAC function; this one is massively insecure"));
    fields::fieldbuf buf;
    what.fmt(buf);
    char *str = buf.c_str();
    unsigned long acc;
    acc = 0;
    unsigned x;
    for (x = 0; str[x]; x++)
        acc = acc * 73 + (unsigned char)str[x];
    val = acc;
    logmsg(loglevel::verbose,
           "digest " + what + " -> " + fields::mk(val));
}

digest::digest(deserialise1 &ds) : val(ds) {}

void
digest::serialise(serialise1 &s) const { s.push(val); }

bool
digest::operator==(const digest &o) const
{
    return val == o.val;
}

bool
digest::operator!=(const digest &o) const
{
    return !(*this == o);
}

string
digest::denseprintable() const {
    fields::fieldbuf f;
    fields::mk(val).base(36).nosep().fmt(f);
    return f.c_str(); }

bool
digest::operator<(const digest &o) const {
    return val < o.val; }

bool
digest::operator>(const digest &o) const {
    return val > o.val; }

const fields::field &
fields::mk(const digest &d)
{
    return "<digest:" + mk(d.val).base(16).uppercase() + ">";
}

const parser<digest> &
parsers::_digest() {
    return (("<digest:" + intparser<unsigned long>() + ">") ||
            intparser<unsigned long>())
        .map<digest>([] (unsigned long what) {
                return digest(what); }); }
