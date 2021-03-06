/* This really doesn't deserve to be called a digest */
#include "digest.H"

#include "either.H"
#include "fields.H"
#include "logging.H"
#include "parsers.H"
#include "serialise.H"
#include "string.H"

#include "parsers.tmpl"

digest::digest(const fields::field &what)
{
    logmsg(loglevel::error,
           fields::mk(
               "need to implement a proper MAC function; this one "
               "is massively insecure"));
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
           "digest " + what + " -> " + fields::mk(*this));
}

digest::digest(deserialise1 &ds) : val(ds) {}

void
digest::serialise(serialise1 &s) const { s.push(val); }

string
digest::denseprintable() const {
    fields::fieldbuf f;
    fields::mk(val).base(36).nosep().fmt(f);
    return f.c_str(); }

const fields::field &
digest::field() const {
    return "<digest:" + fields::mk(val).base(16).uppercase() + ">"; }

const ::parser<digest> &
digest::parser() {
    class inner : public ::parser<digest> {
    public: const ::parser<unsigned long> &p;
    public: inner() : p(parsers::intparser<unsigned long>()) {}
    public: orerror<result> parse(const char *what) const {
        auto i((("<digest:" + p + ">") || p).parse(what));
        if (i.isfailure()) return i.failure();
        else return result(i.success().left, digest(i.success().res)); } };
    return *new inner(); }
