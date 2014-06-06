#include "error.H"

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "logging.H"

const error error::disconnected(-1);
const error error::overflowed(-2);

const char *
error::str() const
{
    if (e == -1)
	return "disconnected";
    else if (e == -2)
	return "overflowed";
    else if (e > 0)
	return strerror(e);
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
