#include "error.H"

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

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

const char *
error::str() const
{
    if (e > 0)
        return strerror(e);
    else if (*this == unknown)
        return "unknown";
    else if (*this == disconnected)
        return "disconnected";
    else if (*this == overflowed)
        return "overflowed";
    else if (*this == underflowed)
        return "underflowed";
    else if (*this == missingparameter)
        return "missingparameter";
    else if (*this == invalidmessage)
        return "invalidmessage";
    else if (*this == unrecognisedmessage)
        return "unrecognisedmessage";
    else if (*this == noparse)
        return "noparse";
    else
        abort();
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
error::fatal(const char *msg) const
{
    logmsg(loglevel::emergency, "fatal error: %s: %s",
           msg, str());
    errx(1, "fatal error: %s: %s", msg, str());
}

void
error::warn(const char *msg) const
{
    logmsg(loglevel::failure, "warning: %s: %s",
           msg, str());
    warnx("warning: %s: %s", msg, str());
}

wireproto_simple_wrapper_type(error, int, e);
