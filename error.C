#include "error.H"

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "fields.H"
#include "logging.H"
#include "wireproto.tmpl"

const error error::unknown(0);
const error error::disconnected(-1);
const error error::overflowed(-2);
const error error::underflowed(-3);
const error error::missingparameter(-4);
const error error::invalidmessage(-5);
const error error::unrecognisedmessage(-6);
const error error::noparse(-7);
const error error::timeout(-8);

class errorfield : public fields::field {
    error content;
    errorfield(const error &_content)
        : content(_content)
        {}
public:
    static const field &n(const error &content)
        { return *new errorfield(content); }
    void fmt(fields::fieldbuf &buf) const
        {
            if (content.e > 0) {
                buf.push(strerror(content.e));
            } else if (content == error::unknown) {
                buf.push("unknown");
            } else if (content == error::disconnected) {
                buf.push("disconnected");
            } else if (content == error::overflowed) {
                buf.push("overflowed");
            } else if (content == error::underflowed) {
                buf.push("underflowed");
            } else if (content == error::missingparameter) {
                buf.push("missingparameter");
            } else if (content == error::invalidmessage) {
                buf.push("invalidmessage");
            } else if (content == error::unrecognisedmessage) {
                buf.push("unrecognisedmessage");
            } else if (content == error::noparse) {
                buf.push("noparse");
            } else if (content == error::timeout) {
                buf.push("timeout");
            } else {
                ("<invalid error " + fields::mk(content.e) + ">").fmt(buf);
            }
        }
};
const fields::field &
fields::mk(const error &e)
{
    return errorfield::n(e);
}

error
error::from_errno()
{
    int e = errno;
    errno = 99; /* deliberately clobber errno to catch people using it
                   when they should be using a returned error code. */
    return error(e);
}

error
error::from_errno(int err)
{
    return error(err);
}

void
error::fatal(const fields::field &msg) const
{
    logmsg(loglevel::emergency,
           "fatal error: " + msg + ": " + fields::mk(*this));
    fields::fieldbuf buf;
    (msg + ": " + fields::mk(*this)).fmt(buf);
    errx(1, "fatal error: %s", buf.c_str());
}

void
error::fatal(const char *msg) const
{
    fatal(fields::mk(msg));
}

void
error::warn(const fields::field &msg) const
{
    logmsg(loglevel::failure,
           "warning: " + msg + ": " + fields::mk(*this));
    fields::fieldbuf buf;
    (msg + ": " + fields::mk(*this)).fmt(buf);
    warnx("warning: %s", buf.c_str());
}

void
error::warn(const char *msg) const
{
    warn(fields::mk(msg));
}

wireproto_simple_wrapper_type(error, int, e);
