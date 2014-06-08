#include "util.H"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

#include "error.H"
#include "orerror.H"

orerror<long>
parselong(const char *what)
{
    char *end;
    long res;
    errno = 0;
    res = strtol(what, &end, 0);
    if (end == what)
        return error::noparse;
    if (errno)
        return error::from_errno();
    while (isspace(*end))
        end++;
    if (*end != '\0')
        return error::noparse;
    return res;
}
