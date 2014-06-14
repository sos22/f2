/* This really doesn't deserve to be called a digest */
#include "digest.H"

#include "fields.H"
#include "logging.H"

digest::digest(unsigned long _val)
    : val(_val)
{}

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
        acc = acc * 73 + str[x];
    val = acc;
}

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
